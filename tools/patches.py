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

    PATCHES_142[0x7000xxxxxx] = _aspect_load

BOTWRecompAndroid's patches (its frame pacing, widescreen and a reload crash, at BOTW's addresses) are in this file's
git history and docs/botw/BOTW-PORT.md: the runtime functions they called (rt_frame_ticks, rt_frame_wait,
rt_vfr_update, rt_persp_update, rt_persp_aspect) are still there, unused until TOTK's own sites are found.
"""
import struct  # noqa: F401 (the patches' instruction checks)

PATCHES_142 = {}  # Tears of the Kingdom 1.4.2 (main build 5cb42b1c...): none yet
BUILD_TOTK_1_4_2 = '5cb42b1cf25469fb'


def patches_for(build_id):
    """The patches for a module, by its build id (sdk and anything unknown: none)."""
    return {BUILD_TOTK_1_4_2: PATCHES_142}.get(build_id[:16], {})
