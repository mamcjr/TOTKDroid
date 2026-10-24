# TOTKRecompAndroid

**English** | [Português](README.pt-BR.md)

A static recompilation of *The Legend of Zelda: Tears of the Kingdom* (Nintendo Switch, version **1.4.2**) for
Android, in its first steps. This is **not emulation**: the game's ARM64 code becomes C compiled for your phone, and its
graphics (NVN) are translated to Vulkan.

It is built on the engine of **[LAS Recomp](https://github.com/peiikai/LASRecomp)** by peiikai (a recompilation of
*Link's Awakening*), its Android port **[LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid)** and
**[BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid)**, which this repository was forked from.

> [!WARNING]
> **Not playable yet.** The game boots on the phone to its title screen (with the Turnip driver); a new game crashes
> as its audio starts. The plan and the state of each phase are in [docs/TOTK-PORT.md](docs/TOTK-PORT.md) (in
> Portuguese).

## Supported platform

Android only (arm64, Vulkan). Windows, Linux or macOS may be used as development hosts with the Android SDK/NDK.

## Status

- The app (start screen, settings, touch controls, controllers, mods, Vulkan driver choice) comes from
  BOTWRecompAndroid and builds.
- TOTK 1.4.2 runs on the very same Nintendo SDK as BOTW 1.8.2: its `sdk` and `multimedia` modules are the same files,
  and it asks for the same 49 system services. Everything the engine learned from BOTW 1.8.2 (SDK 20.5.6, NVN 55.15)
  applies as it is; only the game's own code (`main`) is new.
- The game's code generates and builds: 228,251 functions from `main` and 40,461 from the SDK, in a 273 MB
  `totkgame.so`.
- On the phone (POCO F7 Ultra, the Turnip driver): the game boots to its **title screen** at 30 fps - the logo, the
  animated Zonai murals, the menu in Portuguese - and the touch controls move through it. With Qualcomm's own driver
  the GPU is lost right after the opening.
- Not done yet: a new game (it crashes as its audio starts: the audio renderer's newer revision), Opus audio (the
  app's FFmpeg has no Opus decoder: silence for now), movies, saves, widescreen, performance.

> [!IMPORTANT]
> Like LAS Recomp, this project is made almost entirely with AI (Claude, by Anthropic, through Claude Code), directed,
> tested and reviewed by a human.

> [!NOTE]
> **No game files are included.** You need your own legally obtained dump of *Tears of the Kingdom* with the 1.4.2
> update applied (`exefs` and `romfs`, extracted). Instructions on how to obtain it will not be provided.

## Building (developers)

With the Android SDK / NDK and your dump in `Dump/0100F2C0115B6000/` (`exefs/` and `romfs/`; the folder is ignored by
git), with Python and the `lz4`, `capstone` and `unicorn` packages:

```bash
cd tools && python totk_dump.py                              # check the dump (modules, version, romfs)
cd android/totkrecompandroid && ./gradlew assembleDebug      # the app
android/build_gamelib.sh Dump/0100F2C0115B6000/exefs         # the game code (hours)
ROMFS_HOST=1 android/push_game.sh Dump/0100F2C0115B6000      # game files and game code onto the phone
android/run_boot.sh 120                                      # start the game, log to android/out/boot.log
```

## Credits

- [peiikai](https://github.com/peiikai) for [LAS Recomp](https://github.com/peiikai/LASRecomp), the recompiler and
  runtime this project starts from (MIT, see [LICENSE](LICENSE)). Its original README is kept in [docs/las/](docs/las/).
- [LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid), its Android port, and
  [BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid) (its README in [docs/botw/](docs/botw/)): the app,
  the phone-side game code build, touch controls, mods, driver compatibility, the Portuguese menus, the Sheikah look and
  everything the engine learned from BOTW 1.8.2's SDK.
- PKMSVRecompAndroid (a sibling port of *Pokémon Scarlet*, not published): the version handling for a game with one
  version, the program id from the NPDM and the 16-byte exclusive pairs in the recompiler.
- [Revivedx](https://github.com/Revivedx) for [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp) (MIT):
  the SDK 20 kernel fixes, setjmp, NVN 55.
- [Nycolazs](https://github.com/Nycolazs) for LASRecomp-Android-Plus (MIT): Vulkan driver compatibility.
- The [zeldaret/botw](https://github.com/zeldaret/botw) decompilation and [open-ead/sead](https://github.com/open-ead/sead),
  for what is known about the engine TOTK grew from.
- Upscaling and anti-aliasing: [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) (MIT),
  [Qualcomm Snapdragon GSR](https://github.com/SnapdragonStudios/snapdragon-gsr) (BSD-3-Clause),
  [SMAA](https://github.com/iryoku/smaa) (MIT).
- Libraries: [SDL](https://libsdl.org) (zlib), [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [libadrenotools](https://github.com/bylaws/libadrenotools) (BSD-2-Clause), [volk](https://github.com/zeux/volk) (MIT),
  [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (MIT),
  [shaderc](https://github.com/google/shaderc) (Apache 2.0), [FFmpeg](https://ffmpeg.org) (LGPL),
  [LZ4](https://github.com/lz4/lz4) (BSD-2-Clause), [Capstone](https://www.capstone-engine.org) (BSD-3-Clause),
  [Unicorn](https://www.unicorn-engine.org) (GPLv2, built from its source by `android/deps/build_unicorn.sh`),
  [Python](https://www.python.org) (PSF) and [Zig](https://ziglang.org) (MIT) for the phone-side build.
- [Mesa](https://www.mesa3d.org)'s Turnip driver (MIT), which players load themselves.
- Fonts: [Baloo 2](https://github.com/EkType/Baloo2) (OFL), DejaVu Sans (Bitstream Vera license). The Android robot
  is reproduced or modified from work created and shared by Google (CC BY 3.0).
- Made with [Claude Code](https://claude.com/claude-code) (Anthropic).

*The Legend of Zelda* and *Tears of the Kingdom* are trademarks of Nintendo. This project is not affiliated with or
endorsed by Nintendo.
