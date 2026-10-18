# Building on Windows

Install Visual Studio 2022 C++ Build Tools with CMake support and the Windows SDK. The launcher uses the .NET Framework C# compiler included with Windows.

From the repository root, run:

```powershell
powershell -ExecutionPolicy Bypass -File launcher/build.ps1
```

The resulting launcher is `dist/DuctTapeDoom.exe`. The script embeds the engine, launcher image and the three mod archives. Vendored dependency sources are in `dependencies/`. No commercial IWAD is supplied.

To enable gameplay/GPU checks, configure with your own IWAD:

```powershell
cmake -S duct-tape-doom-main -B build -G "Visual Studio 17 2022" -A x64 -DDOOM_BUILD_SDL=ON -DDOOM_RENDER3D=ON -DBUILD_TESTING=ON -DDOOM_TEST_GPU=ON -DDOOM_GAME_DATA="C:/Games/Doom/DOOM.WAD"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

GPU tests create game windows. The original engine documentation is preserved in `duct-tape-doom-main/README.md`; the root README describes this customized build. The weapon-switch capture data and its regeneration procedure are documented in `duct-tape-doom-main/tools/switch-reference/README.txt`.
