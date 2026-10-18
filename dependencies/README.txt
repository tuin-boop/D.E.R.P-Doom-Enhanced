Corresponding pinned dependency sources are included here. To rebuild offline,
add these CMake cache options to the configure line in launcher/build.ps1:
-DFETCHCONTENT_SOURCE_DIR_SDL3=<absolute path>/dependencies/sdl3-src
-DFETCHCONTENT_SOURCE_DIR_NUKEDOPL3=<absolute path>/dependencies/nukedopl3-src
-DFETCHCONTENT_SOURCE_DIR_TINYSOUNDFONT=<absolute path>/dependencies/tinysoundfont-src
The normal build script downloads the same pinned dependencies if needed.
No IWAD or mod files are included. Supply those separately at runtime.
