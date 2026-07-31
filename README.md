# DovaJump

This is not the final version of the code—merely a stable, properly functioning one. The build is currently under development.

I would like to express my sincere gratitude to the author of the BetterJumping mod for making their plugin's source code publicly available.
I studied the code thoroughly and figured out how to implement my own ideas.

## Requirements

* [CMake](https://cmake.org/) (≥ 3.24)
    * Add this to your `PATH`
* [The Elder Scrolls V: Skyrim Special Edition](https://store.steampowered.com/app/489830)
    * (Optional) Add the environment variable `CompiledPluginsPath` to point to the folder where you want the .dll to be copied after building
* [Vcpkg](https://github.com/microsoft/vcpkg) — required for spdlog dependency
    * Add the environment variable `VCPKG_ROOT` with the value as the path to the folder containing vcpkg
* [Visual Studio Community 2022](https://visualstudio.microsoft.com/)
    * Desktop development with C++
* [Git](https://git-scm.com/) — to clone the repository with submodules
* [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG/tree/v3.4.0)
    * This is included as a submodule in `external/CommonLibSSE-NG`. No separate environment variable is required if you clone with `--recursive`.

## User Requirements (in-game)

* [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Building

Clone the repository with submodules:

```bash
git clone --recursive https://github.com/your-username/DovaJump.git
cd DovaJump
