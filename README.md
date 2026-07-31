# DovaJump

This is not the final version of the code—merely a stable, properly functioning one. The build is currently under development.

I would like to express my sincere gratitude to the author of the BetterJumping mod for making their plugin's source code publicly available. I studied the code thoroughly and figured out how to implement my own ideas.

---

## Requirements

### Build tools
* [CMake](https://cmake.org/) (≥ 3.24)  
  Add it to your `PATH`.
* [Visual Studio Community 2022](https://visualstudio.microsoft.com/)  
  With the *Desktop development with C++* workload.
* [Git](https://git-scm.com/) – to clone the repository and its submodules.
* [Vcpkg](https://github.com/microsoft/vcpkg) – required for the `spdlog` dependency (used by CommonLibSSE-NG).  
  You do **not** need to set the `VCPKG_ROOT` environment variable if you specify the toolchain file directly (e.g., via `-DCMAKE_TOOLCHAIN_FILE=...` or in `CMakePresets.json`). The vcpkg installation itself must be present.

### Game & runtime
* [The Elder Scrolls V: Skyrim Special Edition](https://store.steampowered.com/app/489830)  
  (Optional) Add the environment variable `CompiledPluginsPath` to point to the folder where you want the built `.dll` to be copied after building.
* [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444) – required at runtime.

> **Note**: CommonLibSSE-NG is included as a Git submodule in `external/CommonLibSSE-NG`. No separate environment variable is required for it.

---

## Building

### 1. Clone the repository with submodules
```bash
git clone --recursive https://github.com/your-username/DovaJump.git
cd DovaJump
If you forgot --recursive, you can initialise the submodules later:

bash
git submodule init
git submodule update
2. Configure and build
You have two options:

Option A – Use the CMake preset (recommended if you have CMakePresets.json)
bash
cmake --preset x64-Release
cmake --build out/build/x64-Release --config Release
The preset already contains the vcpkg toolchain and triplet settings (e.g., x64-windows-static-md). Adjust the preset name if yours differs.

Option B – Manual CMake call (no preset)
bash
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build . --config Release
Make sure to change the toolchain path and triplet to match your environment.

3. Output
After a successful build, the plugin DovaJump.dll is placed in:

out/build/x64-Release/ (when using the preset)

build/Release/ (when using manual CMake)

Copy the .dll to your Skyrim Data/SKSE/Plugins/ folder.
If you have set the CompiledPluginsPath environment variable, your post‑build script (if any) may copy it automatically.

Note: If you use a different triplet (e.g., x64-windows), install the corresponding vcpkg packages and adjust the CMake arguments accordingly.

Happy jumping!
