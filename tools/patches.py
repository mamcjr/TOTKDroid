"""Per-instruction replacements applied by gen.py (guest address -> function(gen, addr) returning C code).

Each entry replaces the one instruction at that address of `main` (canonical address, 0x7000000000 + offset) with the C
the function returns; gen.py rebases the addresses in it for --dyn-base. Check the instruction word first (an assert),
so a wrong build fails here instead of patching the wrong code. Example, from LAS Recomp's widescreen: a camera's
`ldr sN, [xM, #imm]` of the 16:9 aspect literal, replaced by a call into the runtime:

    def _aspect_load(gen, a):
        w = struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]
        assert (w & 0xffc00000) == 0xbd400000, hex(w)  # ldr s, [x, #imm]
        rt = w & 31
        return (f'{{ extern float rt_aspect_site(int); float f = rt_aspect_site(0); '
                f'c->v[{rt}] = 0; memcpy(&c->v[{rt}], &f, 4); }}  /* {a:#x}: ldr s{rt}, =16/9 */')

BOTWRecompAndroid's patches (its frame pacing, widescreen and a reload crash, at BOTW's addresses) are in this file's
git history and docs/botw/BOTW-PORT.md.

Tears of the Kingdom 1.4.3's sites come from MacNativeGames (github.com/ly05010419/MacNativeGames,
games/totk/runtime/game_patches_v143.c), which runs the same build on Macs and checks each original instruction the
same way: its addresses and words matched this build's main one for one.
"""
import struct

BUILD_TOTK_1_4_2 = '5cb42b1cf25469fb'
BUILD_TOTK_1_4_3 = '277178b7dba1b6d4'
PATCHES_142 = {}  # Tears of the Kingdom 1.4.2 (main build 5cb42b1c...): none
PATCHES_143 = {}  # Tears of the Kingdom 1.4.3 (main build 277178b7...)


def _word(gen, a):
    return struct.unpack_from('<I', gen.nso.image, a - gen.base)[0]


def _expect(gen, a, word):
    w = _word(gen, a)
    assert w == word, f'{a:#x}: {w:08x}, expected {word:08x} (not TOTK 1.4.3?)'


# The dynamic resolution controller (main+0x584300, from the renderer's per-frame update) steps the scene through
# 100 / 90 / 80 % tiers (1600x896, 1432x808, 1280x720) from its frame-time prediction and returns the tier's scale in
# x0's low word; every path leaves through this one move. Each step gave a frame whose world wasn't drawn (black but
# for the particles and the HUD): runtime rt_dynres_scale holds it at 100 % unless LARECOMP_DYNRES=1.
def _dynres_scale(gen, a):
    _expect(gen, a, 0x2a1703e9)  # mov w9, w23
    return ('{ extern uint32_t rt_dynres_scale(uint32_t); c->x[9] = (uint64_t)rt_dynres_scale((uint32_t)c->x[23]); }'
            f'  /* {a:#x}: mov w9, w23 (the tier\'s scale) */')


# The world's frame step: FrameRateMgr derives it from the nominal frame rate ([x19+0x250], 30), as 60/fps x 0.5 (the
# step relative to 30 fps, stored for the frame) and 60/fps x the half-step selector (the interval in 60 Hz vsyncs).
# A phone that renders 22 fps then played in slow motion. runtime rt_frame_ratio30 / rt_frame_interval60 put the
# measured time between presents in their place (LARECOMP_REALTIME=0: the nominal values, as the original computes).
def _step_ratio_load(gen, a):
    _expect(gen, a, 0xbd425260)  # ldr s0, [x19, #0x250]
    return ('{ extern float rt_frame_ratio30(float); int32_t fps = (int32_t)LD32(c->x[19] + 0x250ull); '
            'float f = rt_frame_ratio30(30.0f / (float)fps); c->v[0] = 0; memcpy(&c->v[0], &f, 4); }'
            f'  /* {a:#x}: ldr s0, [x19, #0x250] -> the step ratio itself */')


def _drop(word, what):
    def patch(gen, a):
        _expect(gen, a, word)
        return f'/* {a:#x}: {what} (tools/patches.py: the ratio is already in s0) */'
    return patch


def _step_interval(gen, a):
    _expect(gen, a, 0x1e210808)  # fmul s8, s0, s1
    return ('{ extern float rt_frame_interval60(float); float s0, s1; memcpy(&s0, &c->v[0], 4); memcpy(&s1, &c->v[1], 4); '
            's0 = rt_frame_interval60(s0); float s8 = s0 * s1; c->v[0] = 0; memcpy(&c->v[0], &s0, 4); '
            'c->v[8] = 0; memcpy(&c->v[8], &s8, 4); }'
            f'  /* {a:#x}: fmul s8, s0, s1 (s0: 60 / fps) */')


PATCHES_143[0x70005848a8] = _dynres_scale
PATCHES_143[0x70002daa54] = _step_ratio_load
PATCHES_143[0x70002daa6c] = _drop(0x5e21d800, 'scvtf s0, s0')
PATCHES_143[0x70002daa70] = _drop(0x1e201820, 'fdiv s0, s1, s0')
PATCHES_143[0x70002daa78] = _drop(0x1e210800, 'fmul s0, s0, s1')
PATCHES_143[0x70003e5658] = _step_interval


def patches_for(build_id):
    """The patches for a module, by its build id (sdk and anything unknown: none)."""
    return {BUILD_TOTK_1_4_2: PATCHES_142, BUILD_TOTK_1_4_3: PATCHES_143}.get(build_id[:16], {})
