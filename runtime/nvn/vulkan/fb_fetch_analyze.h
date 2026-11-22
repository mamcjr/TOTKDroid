#pragma once
// Static 1:1 G-buffer framebuffer-fetch prover for translated Maxwell VS+FS GLSL.
// Included by runtime/nvn/vulkan/exec.c and hashed into AOT_TRANS_SRC_HASH so any change
// here automatically invalidates stale fb_fetch_cand_mask entries in prog_manifest.bin.
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Registers a GLSL line of the translator's output assigns ("rN = ...", anywhere in the line, not "=="): count, and
// up to 16 of them in out[]. (Texture results and swaps land in a register mid-line: `{ vec4 t_ = ...; r5 = ...; }`.)
static int line_assigned_regs(const char *line, uint8_t *out) {
    int n = 0;
    for (const char *q = line; *q; q++) {
        if (*q != 'r' || (q > line && (isalnum((unsigned char)q[-1]) || q[-1] == '_' || q[-1] == '.'))) continue;
        const char *d = q + 1;
        if (*d < '0' || *d > '9') continue;
        int r = 0;
        while (*d >= '0' && *d <= '9') r = r * 10 + (*d++ - '0');
        if (isalnum((unsigned char)*d) || *d == '_') continue;
        while (*d == ' ') d++;
        if (d[0] != '=' || d[1] == '=') continue;
        if (r < 256 && n < 16) out[n] = (uint8_t)r;
        n++;
    }
    return n;
}

// A line opening a loop (the translator's `for (;;)`, the branchy `for (guard ...) switch (pc)` state machine, while).
// (not a one-line loop with no register in it: the local-memory clear `... for (int li_ ...) lmem[li_] = 0u;`)
static bool line_opens_loop(const char *line) {
    return !strncmp(line, "for (", 5) || !strncmp(line, "while (", 7) || !strncmp(line, "switch (", 8);
}

// The registers assigned anywhere in the block that opens on the line at src (up to its matching brace), as a bitset:
// inside a loop a register may hold what a later line of the body (an earlier iteration) or another `case` gave it.
static void block_assigned_regs(const char *src, uint8_t set[32]) {
    memset(set, 0, 32);
    int depth = 0;
    for (const char *p = src; *p;) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[1024];
        size_t cl = len < sizeof line - 1 ? len : sizeof line - 1;
        memcpy(line, p, cl); line[cl] = 0;
        if (len >= sizeof line) memset(set, 0xFF, 32);
        uint8_t asg[16];
        int na = line_assigned_regs(line, asg);
        for (int i = 0; i < na && i < 16; i++) set[asg[i] >> 3] |= (uint8_t)(1u << (asg[i] & 7));
        if (na > 16) memset(set, 0xFF, 32);
        for (size_t i = 0; i < len; i++) depth += p[i] == '{' ? 1 : p[i] == '}' ? -1 : 0;
        if (depth <= 0) break;
        p = eol ? eol + 1 : p + len;
    }
}

// Statically proves which fragment texture slots (0..15) sample ONLY the exact 1:1 current fragment pixel:
// VS must compute vK.w = gl_Position.w, vK.x = (w + pos.x) * 0.5, vK.y = (w - pos.y) * 0.5 (with negative-height
// Vulkan viewport and no post-body flip_y/flip_x);
// FS must sample tS ONLY with vec2(vK.x * (1/vK.w), vK.y * (1/vK.w)) (no offset, refraction, gather, or tsize).
// Conservative: the VS is tracked by value (each register assignment a new value id; a value assigned inside an `if`
// is unknown after it), the FS by register state with `if`/`else` merging; loops/switches invalidate all registers
// written in their bodies.
static uint16_t analyze_1to1_gbuf_slots(const char *vs, const char *fs, bool flip_y, bool flip_x) {
    if (!vs || !fs || flip_y || flip_x) return 0;
    // ---- vertex shader: value ids
    int32_t val[256];
    for (int i = 0; i < 256; i++) val[i] = i + 1;
    int32_t next_id = 1000;
    int32_t add_res[256], add_a[256], add_b[256], mh_res[256], mh_a[256], mh_b[256];
    uint8_t add_neg[256], mh_neg[256];
    int32_t pos_v[4] = {-1, -1, -1, -1};
    int32_t v_w[32], vx_a[32], vx_b[32], vy_a[32], vy_b[32];
    uint8_t vx_neg[32], vy_neg[32];
    memset(add_res, 0, sizeof add_res); memset(mh_res, 0, sizeof mh_res);
    memset(v_w, -1, sizeof v_w);
    memset(vx_a, -1, sizeof vx_a); memset(vx_b, -1, sizeof vx_b);
    memset(vy_a, -1, sizeof vy_a); memset(vy_b, -1, sizeof vy_b);
    memset(vx_neg, 0, sizeof vx_neg); memset(vy_neg, 0, sizeof vy_neg);
    uint8_t vblk_if[32] = {0}, vblk_loop[32] = {0}, vblk_set[32][32], vloop_set[32][32];  // open blocks: `if` / loop,
    int vdepth = 0, vif = 0, vloop = 0;                                                    // registers assigned inside

    for (const char *p = vs; *p;) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        const char *s = p;
        while (len && (*s == ' ' || *s == '\t')) s++, len--;
        if (len > 0) {
            if (len >= 1024) return 0;  // conservatively bail on unexpectedly huge line
            char line[1024];
            memcpy(line, s, len); line[len] = 0;
            bool has_open = strchr(line, '{') != NULL, has_close = strchr(line, '}') != NULL;
            if (!strncmp(line, "} else", 6)) {
                // (same `if` block continues: its assigned set keeps growing)
            } else if (has_open && !has_close) {
                if (vdepth == 32) return 0;
                vblk_if[vdepth] = !strncmp(line, "if (", 4);
                vblk_loop[vdepth] = line_opens_loop(line);
                memset(vblk_set[vdepth], 0, 32);
                if (vblk_loop[vdepth]) {  // a loop: what its body assigns is unknown on entry (a later iteration's)
                    block_assigned_regs(s, vloop_set[vdepth]);
                    for (int r = 0; r < 256; r++)
                        if (vloop_set[vdepth][r >> 3] & (1u << (r & 7))) val[r] = next_id++;
                }
                vif += vblk_if[vdepth];
                vloop += vblk_loop[vdepth];
                vdepth++;
            } else if (has_close && !has_open && vdepth > 0) {
                vdepth--;
                vif -= vblk_if[vdepth];
                vloop -= vblk_loop[vdepth];
                for (int r = 0; r < 256; r++) {
                    if (!(vblk_set[vdepth][r >> 3] & (1u << (r & 7)))) continue;
                    if (vblk_if[vdepth] || vblk_loop[vdepth]) val[r] = next_id++;  // either value now: unknown
                    if (vdepth > 0) vblk_set[vdepth - 1][r >> 3] |= (uint8_t)(1u << (r & 7));
                }
            }
            if (!strncmp(line, "case ", 5) || !strncmp(line, "default:", 8)) {
                for (int d = 0; d < vdepth; d++)  // (a state machine's block: entered from any other)
                    if (vblk_loop[d])
                        for (int r = 0; r < 256; r++)
                            if (vloop_set[d][r >> 3] & (1u << (r & 7))) val[r] = next_id++;
            }
            if (vif > 0 && !vloop && (strstr(line, "break;") || strstr(line, "exit_now") || strstr(line, "return"))) return 0;
            uint8_t asg[16];
            int na = line_assigned_regs(line, asg);
            int c, r0, r1, r2, vk;
            int32_t nres_kind = 0, na_v = -1, nb_v = -1; uint8_t nneg = 0;  // 1 add, 2 mul-half
            int lead = -1;
            if (sscanf(line, "gl_Position[%d] = uintBitsToFloat(r%d);", &c, &r0) == 2 && c >= 0 && c < 4 && r0 >= 0 && r0 < 256) {
                pos_v[c] = vif || vloop ? -1 : val[r0];
            } else if (sscanf(line, "v%d[%d] = uintBitsToFloat(r%d);", &vk, &c, &r0) == 3 && vk >= 0 && vk < 32 && r0 >= 0 && r0 < 256) {
                bool mh = !vif && !vloop && mh_res[r0] == val[r0];
                if (c == 3) v_w[vk] = vif || vloop ? -1 : val[r0];
                else if (c == 0) { vx_a[vk] = mh ? mh_a[r0] : -1; vx_b[vk] = mh ? mh_b[r0] : -1; vx_neg[vk] = mh_neg[r0]; }
                else if (c == 1) { vy_a[vk] = mh ? mh_a[r0] : -1; vy_b[vk] = mh ? mh_b[r0] : -1; vy_neg[vk] = mh_neg[r0]; }
            } else if (na == 1 && sscanf(line, "r%d = floatBitsToUint((uintBitsToFloat(r%d) + uintBitsToFloat(r%d)));", &r0, &r1, &r2) == 3 &&
                       r0 >= 0 && r0 < 256 && r1 >= 0 && r1 < 256 && r2 >= 0 && r2 < 256) {
                lead = r0, nres_kind = 1, na_v = val[r1], nb_v = val[r2], nneg = 0;
            } else if (na == 1 && sscanf(line, "r%d = floatBitsToUint((uintBitsToFloat(r%d) + (-uintBitsToFloat(r%d))));", &r0, &r1, &r2) == 3 &&
                       r0 >= 0 && r0 < 256 && r1 >= 0 && r1 < 256 && r2 >= 0 && r2 < 256) {
                lead = r0, nres_kind = 1, na_v = val[r1], nb_v = val[r2], nneg = 1;
            } else if (na == 1 && sscanf(line, "r%d = floatBitsToUint((uintBitsToFloat(r%d) * uintBitsToFloat(0x3f000000u)));", &r0, &r1) == 2 &&
                       r0 >= 0 && r0 < 256 && r1 >= 0 && r1 < 256 && add_res[r1] == val[r1]) {
                lead = r0, nres_kind = 2, na_v = add_a[r1], nb_v = add_b[r1], nneg = add_neg[r1];
            }
            if (na > 16) {
                for (int r = 0; r < 256; r++) val[r] = next_id++;
                if (vdepth > 0) memset(vblk_set[vdepth - 1], 0xFF, 32);
            } else {
                for (int i = 0; i < na; i++) {
                    val[asg[i]] = next_id++;
                    if (vdepth > 0) vblk_set[vdepth - 1][asg[i] >> 3] |= (uint8_t)(1u << (asg[i] & 7));
                }
            }
            if (lead >= 0 && nres_kind == 1) add_res[lead] = val[lead], add_a[lead] = na_v, add_b[lead] = nb_v, add_neg[lead] = nneg;
            if (lead >= 0 && nres_kind == 2) mh_res[lead] = val[lead], mh_a[lead] = na_v, mh_b[lead] = nb_v, mh_neg[lead] = nneg;
        }
        p = eol ? eol + 1 : p + strlen(p);
    }
    if (pos_v[0] < 0 || pos_v[1] < 0 || pos_v[3] < 0) return 0;
    uint32_t sv_mask = 0;
    for (int k = 0; k < 32; k++) {
        if (v_w[k] < 0 || v_w[k] != pos_v[3]) continue;
        bool xok = vx_a[k] >= 0 && !vx_neg[k] &&
                   ((vx_a[k] == pos_v[3] && vx_b[k] == pos_v[0]) || (vx_a[k] == pos_v[0] && vx_b[k] == pos_v[3]));
        bool yok = vy_a[k] >= 0 && vy_neg[k] && vy_a[k] == pos_v[3] && vy_b[k] == pos_v[1];
        if (xok && yok) sv_mask |= 1u << k;
    }
    if (!sv_mask) return 0;

    // ---- fragment shader: register state, merged over if/else. A varying read is vK[c] times an optional
    // gl_FragCoord.w and an optional register (IPA multiply: Maxwell's 1/w); x/w only cancel to the screen UV when
    // both carry the same factors, so each tracked value keeps its factor signature (fw, value id of the register).
    const char *mpos = strstr(fs, "void main()");
    if (!mpos) return 0;
    typedef struct { uint8_t kind[256], var[256], fw[256]; int32_t mv[256]; } FsRegs;  // kind 1=vx 2=vy 3=vw 4=invw 5=uvx 6=uvy
    typedef struct { FsRegs cur, snap[16]; uint8_t dirty[16][256]; int32_t fval[256]; } FsState;
    FsState *F = calloc(1, sizeof *F);
    if (!F) return 0;
    for (int i = 0; i < 256; i++) F->fval[i] = i + 1;
    int32_t fnext = 1000;
    uint8_t sis_if[16] = {0}, sis_loop[16] = {0}, sloop_set[16][32];
    int sp_d = 0;
    uint32_t proven = 0, bad = 0;
    bool fail = false;
#define FS_SAME(A, B, q) ((A).kind[q] == (B).kind[q] && (A).var[q] == (B).var[q] && (A).fw[q] == (B).fw[q] && (A).mv[q] == (B).mv[q])
#define FS_CLR(R, q) ((R).kind[q] = 0, (R).var[q] = 0, (R).fw[q] = 0, (R).mv[q] = 0)

    for (const char *p = mpos; *p && !fail;) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        const char *s = p;
        while (len && (*s == ' ' || *s == '\t')) s++, len--;
        if (len > 0) {
            if (len >= 1024) { fail = true; break; }
            char line[1024];
            memcpy(line, s, len); line[len] = 0;
            FsRegs *K = &F->cur;
            // `if {A} else {B}`: B starts from the state before the `if`; after it, a register either branch changed is
            // unknown (dirty keeps A's changes across the restore)
            if (!strncmp(line, "} else {", 8)) {
                if (sp_d > 0 && sis_if[sp_d - 1]) {
                    int t = sp_d - 1;
                    for (int q = 0; q < 256; q++)
                        if (!FS_SAME(*K, F->snap[t], q)) F->dirty[t][q] = 1;
                    *K = F->snap[t];
                }
            } else {
                bool has_open = strchr(line, '{') != NULL;
                bool has_close = strchr(line, '}') != NULL;
                if (has_open && !has_close) {
                    if (sp_d == 16) { fail = true; break; }
                    bool is_if = !strncmp(line, "if (", 4);
                    sis_if[sp_d] = is_if ? 1 : 0;
                    sis_loop[sp_d] = line_opens_loop(line);
                    if (is_if) {
                        F->snap[sp_d] = *K;
                        memset(F->dirty[sp_d], 0, sizeof F->dirty[sp_d]);
                    }
                    if (sis_loop[sp_d]) {  // a loop: what its body assigns is unknown on entry (a later iteration's)
                        block_assigned_regs(s, sloop_set[sp_d]);
                        for (int q = 0; q < 256; q++)
                            if (sloop_set[sp_d][q >> 3] & (1u << (q & 7))) FS_CLR(*K, q);
                    }
                    sp_d++;
                } else if (has_close && !has_open) {
                    if (sp_d > 0) {
                        sp_d--;
                        if (sis_if[sp_d]) {
                            for (int q = 0; q < 256; q++)
                                if (F->dirty[sp_d][q] || !FS_SAME(*K, F->snap[sp_d], q)) FS_CLR(*K, q);
                        }
                        if (sis_loop[sp_d])  // (and on the way out: any iteration's)
                            for (int q = 0; q < 256; q++)
                                if (sloop_set[sp_d][q >> 3] & (1u << (q & 7))) FS_CLR(*K, q);
                    }
                }
                if (!strncmp(line, "case ", 5) || !strncmp(line, "default:", 8)) {
                    for (int d = 0; d < sp_d; d++)  // (a state machine's block: entered from any other)
                        if (sis_loop[d])
                            for (int q = 0; q < 256; q++)
                                if (sloop_set[d][q >> 3] & (1u << (q & 7))) FS_CLR(*K, q);
                }
            }
            // Check texture calls on tN
            const char *tp;
            if ((tp = strstr(line, "texture(t")) != NULL || (tp = strstr(line, "textureLod(t")) != NULL) {
                int slot = -1, ru = -1, rv = -1;
                const char *tnum = strchr(tp, '(');
                if (tnum) tnum++;
                if (tnum && (sscanf(tnum, "t%d, vec2(uintBitsToFloat(r%d), uintBitsToFloat(r%d))", &slot, &ru, &rv) == 3)) {
                    if (slot >= 0 && slot < 16) {
                        if (ru >= 0 && ru < 256 && rv >= 0 && rv < 256 &&
                            K->kind[ru] == 5 && K->kind[rv] == 6 && K->var[ru] == K->var[rv])
                            proven |= 1u << slot;
                        else
                            bad |= 1u << slot;
                    }
                } else if (tnum && sscanf(tnum, "t%d,", &slot) == 1 && slot >= 0 && slot < 16) {
                    bad |= 1u << slot;
                }
            }
            if ((tp = strstr(line, "textureGather(t")) != NULL || (tp = strstr(line, "texelFetch(t")) != NULL ||
                (tp = strstr(line, "textureGrad(t")) != NULL || (tp = strstr(line, "tsize")) != NULL) {
                int slot = -1;
                const char *d = tp;
                while (*d && (*d < '0' || *d > '9')) d++;
                if (*d && sscanf(d, "%d", &slot) == 1 && slot >= 0 && slot < 16) bad |= 1u << slot;
            }
            // Track register assignments: the leading `rD = <pattern>` (only when it is the line's one assignment);
            // every other register the line assigns becomes unknown
            uint8_t asg[16];
            int na = line_assigned_regs(line, asg);
            int rd = -1, ra, rb, vk, vc, mr;
            uint8_t nk = 0, nv = 0, nfw = 0;
            int32_t nmv = 0;
            if (na == 1 && line[0] == 'r' && sscanf(line, "r%d = ", &rd) == 1 && rd >= 0 && rd < 256) {
                const char *eq = strstr(line, " = ");
                const char *rhs = eq ? eq + 3 : "";
                char chk[96];
                if (sscanf(rhs, "as_u(v%d[%d]", &vk, &vc) == 2 && vk >= 0 && vk < 32 && (sv_mask & (1u << vk)) &&
                    (vc == 0 || vc == 1 || vc == 3)) {
                    bool ok = false;
                    snprintf(chk, sizeof chk, "as_u(v%d[%d]);", vk, vc);
                    if (!strcmp(rhs, chk)) ok = true;
                    snprintf(chk, sizeof chk, "as_u(v%d[%d] * gl_FragCoord.w);", vk, vc);
                    if (!ok && !strcmp(rhs, chk)) ok = true, nfw = 1;
                    if (!ok && sscanf(rhs, "as_u(v%*d[%*d] * uintBitsToFloat(r%d));", &mr) == 1 && mr >= 0 && mr < 256) {
                        snprintf(chk, sizeof chk, "as_u(v%d[%d] * uintBitsToFloat(r%d));", vk, vc, mr);
                        if (!strcmp(rhs, chk)) ok = true, nmv = F->fval[mr];
                    }
                    if (!ok && sscanf(rhs, "as_u(v%*d[%*d] * gl_FragCoord.w * uintBitsToFloat(r%d));", &mr) == 1 && mr >= 0 && mr < 256) {
                        snprintf(chk, sizeof chk, "as_u(v%d[%d] * gl_FragCoord.w * uintBitsToFloat(r%d));", vk, vc, mr);
                        if (!strcmp(rhs, chk)) ok = true, nfw = 1, nmv = F->fval[mr];
                    }
                    if (ok) nk = vc == 0 ? 1 : vc == 1 ? 2 : 3, nv = (uint8_t)vk;
                    else nfw = 0, nmv = 0;
                } else if (sscanf(rhs, "floatBitsToUint((1.0f / uintBitsToFloat(r%d)));", &ra) == 1 &&
                           ra >= 0 && ra < 256 && K->kind[ra] == 3) {
                    nk = 4, nv = K->var[ra], nfw = K->fw[ra], nmv = K->mv[ra];
                } else if (sscanf(rhs, "floatBitsToUint((uintBitsToFloat(r%d) * uintBitsToFloat(r%d)));", &ra, &rb) == 2 &&
                           ra >= 0 && ra < 256 && rb >= 0 && rb < 256 && K->kind[ra] && K->kind[rb] &&
                           K->var[ra] == K->var[rb] && K->fw[ra] == K->fw[rb] && K->mv[ra] == K->mv[rb]) {
                    if ((K->kind[ra] == 1 && K->kind[rb] == 4) || (K->kind[rb] == 1 && K->kind[ra] == 4)) nk = 5;
                    else if ((K->kind[ra] == 2 && K->kind[rb] == 4) || (K->kind[rb] == 2 && K->kind[ra] == 4)) nk = 6;
                    if (nk) nv = K->var[ra], nfw = K->fw[ra], nmv = K->mv[ra];
                } else if (rhs[0] == 'r' && sscanf(rhs, "r%d;", &ra) == 1 && ra >= 0 && ra < 256) {
                    snprintf(chk, sizeof chk, "r%d;", ra);
                    if (!strcmp(rhs, chk)) nk = K->kind[ra], nv = K->var[ra], nfw = K->fw[ra], nmv = K->mv[ra];
                }
            } else rd = -1;
            if (na > 16) {
                for (int q = 0; q < 256; q++) FS_CLR(*K, q), F->fval[q] = fnext++;
            } else {
                for (int i = 0; i < na; i++) FS_CLR(*K, asg[i]), F->fval[asg[i]] = fnext++;
            }
            if (rd >= 0) K->kind[rd] = nk, K->var[rd] = nv, K->fw[rd] = nfw, K->mv[rd] = nmv;
        }
        p = eol ? eol + 1 : p + strlen(p);
    }
#undef FS_SAME
#undef FS_CLR
    free(F);
    return fail ? 0 : (uint16_t)(proven & ~bad);
}
