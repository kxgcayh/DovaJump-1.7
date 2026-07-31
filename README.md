# DovaJump

This is not the final version of the code—merely a stable, properly functioning one. The build is currently under development.

I would like to express my sincere gratitude to the author of the Better Jumping mod (https://www.nexusmods.com/skyrimspecialedition/mods/18967) for making their plugin's source code publicly available. I studied the code thoroughly and figured out how to implement my own ideas.

## Requirements

- CMake (≥ 3.24) – add to PATH
- Visual Studio Community 2022 – with "Desktop development with C++"
- Git – to clone the repository
- Vcpkg – installed at C:\vcpkg (or adjust the path in the preset)
- Skyrim Special Edition
- Address Library for SKSE – required at runtime

## Dependencies (via vcpkg)

The project uses CommonLibSSE‑NG, which depends on several libraries. You must install them via vcpkg before building.

Open a command prompt in your vcpkg folder (C:\vcpkg) and run:
vcpkg install spdlog:x64-windows-static-md

If during CMake configuration you get errors about missing packages (e.g., fmt, etc.), install them the same way:
vcpkg install <package-name>:x64-windows-static-md

The triplet must match the one used in the preset (x64-windows-static-md).

## User Requirements
Address Library for SKSE

## Building

1. Clone the repository with submodules:
git clone --recursive https://github.com/your-username/DovaJump.git
cd DovaJump
If you forgot `--recursive`, you can initialise the submodules later:
git submodule update --init --recursive

**Alternative (if submodules fail for any reason):**  
Download CommonLibSSE‑NG manually from https://github.com/CharmedBaryon/CommonLibSSE-NG/releases/tag/v3.7.0, create the folder `external/CommonLibSSE-NG` in the project root, and extract the archive there. No environment variable is needed.

2. The project includes a CMakePresets.json file that already points to C:\vcpkg and uses the triplet x64-windows-static-md. Just run:
cmake --preset x64-Release
cmake --build out/build/x64-Release --config Release

3. After a successful build, the plugin DovaJump.dll will be placed in out/build/x64-Release/. Copy it to your Skyrim Data/SKSE/Plugins/ folder.

That's it. Happy jumping!
