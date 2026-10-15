# DovaJump (Skyrim AE 1.7.104+ & SE/VR Support)

DovaJump is an SKSE64 gameplay plugin for The Elder Scrolls V: Skyrim that provides advanced aerial acrobatics, including configurable double jumps, wall jumps, enemy stagger jumps, and sprint/charge jump assist.

This repository provides the updated, unified source code for DovaJump with native support for Skyrim Anniversary Edition (1.7.99 / 1.7.104+), pre-load gate compliance for SKSE 2.3.1+, and backward compatibility with Skyrim SE 1.5.97 and AE 1.6.x.

---

## Technical Overview & 1.7.104 Root Cause Analysis

### The Problem in Earlier NG Releases
On Skyrim AE runtime 1.7.99 and 1.7.104, earlier versions of DovaJump NG completely disabled player jumping. The player character remained grounded and unable to perform standard or aerial jumps when pressing the jump key.

### Root Cause: Input Handler Vtable Expansion
In Skyrim runtime 1.7.99 and later, Bethesda updated the underlying input handling subsystems (`PlayerInputHandler` and its subclasses, including `JumpHandler`) to accommodate modern controller input gestures:
- `ProcessMotionGesture`
- `ProcessSixaxis`

These two new virtual methods were inserted above `ProcessButton`, shifting the position of `ProcessButton` within the virtual function table:
- **Skyrim SE 1.5.97 through AE 1.6.1170**: `JumpHandler::ProcessButton` is located at **vtable index 4**.
- **Skyrim AE 1.7.99 and 1.7.104+**: `JumpHandler::ProcessButton` is located at **vtable index 6**.

Upstream DovaJump clones the `JumpHandler` vtable, hooks `CanProcess` (index 1) to intercept jump button events, and chains down to `oldVtable[4]`. On runtime 1.7.104, calling index 4 invokes an empty stub (`retq`), discarding the input event before the engine can register it.

### The Resolution
This fork implements dynamic vtable index resolution based on the detected game binary version:

```cpp
const size_t processButtonIdx = (REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_7_99) ? 6 : 4;
uintptr_t origProcessButtonAddr = reinterpret_cast<uintptr_t>(oldVtable[processButtonIdx]);
std::memcpy(&g_originalProcessButton, &origProcessButtonAddr, sizeof(origProcessButtonAddr));
```

Additionally, `SKSEPlugin_Version` is explicitly defined with SKSE 2.3.1 pre-load gate compliance:
- `versionIndependenceEx = kVersionIndependentEx_AddressLibraryV5 | kVersionIndependentEx_NoStructUse`
- Explicit compatibility listed for `1.7.104`, `1.7.99`, `1.6.1170`, `1.6.1130`, and `1.5.97`.

---

## Compatibility Matrix

| Runtime | Version String | Address Library Database | Status |
| :--- | :--- | :--- | :--- |
| Skyrim AE (Steam / GOG) | 1.7.104 | `versionlib-1-7-104-0.bin` (Format 5) | Fully Supported |
| Skyrim AE | 1.7.99 | `versionlib-1-7-99-0.bin` (Format 5) | Fully Supported |
| Skyrim AE | 1.6.1170 / 1.6.1130 | `versionlib-1-6-1170-0.bin` (Format 2) | Fully Supported |
| Skyrim SE | 1.5.97 | `version-1-5-97-0.bin` (Format 1) | Fully Supported |

### Prerequisites
- [SKSE64](https://skse.silverlock.org/) matching your Skyrim runtime (e.g. SKSE 2.3.1 for AE 1.7.104).
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).
- [Open Animation Replacer (OAR)](https://www.nexusmods.com/skyrimspecialedition/mods/92109) (required for custom jump and wall kick animations).

---

## Installation

### Via Mod Manager (Recommended)
1. Download either:
   - `DovaJump-NG-Patch-v1.1.0-AE17104.zip`: Use if you already have the original Dova Jump mod installed; install this patch after it with overwrite priority.
   - `DovaJump-AllInOne-v1.1.0-AE17104.zip`: Standalone installation containing plugin, configuration, MCM script, and animation assets.
2. Install via Mod Organizer 2 or Vortex.
3. Enable the mod and run Skyrim through `skse64_loader.exe`.

### Manual Installation
Copy the contents of the archive into your Skyrim `Data/` folder:
- `Data/SKSE/Plugins/DovaJump.dll`
- `Data/SKSE/Plugins/JumpRequirements.ini`
- (Optional) `Data/DovaJumpMCM.esp`, `Data/Scripts/`, and `Data/meshes/`

---

## Configuration (`JumpRequirements.ini`)

Located at `Data/SKSE/Plugins/JumpRequirements.ini`:

```ini
[Settings]
SkillType=Sneak       ; Skill governing requirements
MinStamina=0.0        ; Minimum stamina percentage required to perform aerial jumps

[Debug]
EnableLogging=false   ; Write diagnostic trace to Documents/My Games/Skyrim Special Edition/SKSE/DovaJump.log

[DoubleJump]
Level=0               ; Skill level required (0 = unlocked by default)
MaxAirJumps=1         ; Maximum number of mid-air jumps before landing

[WallJump]
Level=0
StaggerCooldown=5.0   ; Seconds before another wall stagger can occur
```

---

## Building from Source

### Option A: Windows (MSVC 2022 + vcpkg + CMake)

1. **Clone repository**:
   ```cmd
   git clone https://github.com/kxgcayh/DovaJump-1.7.git
   cd DovaJump-1.7
   ```

2. **Configure and Build via Preset**:
   Ensure `VCPKG_ROOT` is set in your environment (e.g. `C:\vcpkg`).
   ```cmd
   cmake --preset x64-Release
   cmake --build out/build/x64-Release --config Release
   ```

   Alternatively, use standard CMake:
   ```cmd
   cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake"
   cmake --build build --config Release
   ```

3. The compiled DLL will be located at `out/build/x64-Release/DovaJump.dll` or `build/Release/DovaJump.dll`.

### Option B: macOS / Linux Cross-Compilation (LLVM Clang-cl + MSVC Sysroot)

1. Install LLVM (`brew install llvm ninja cmake`).
2. Configure using your cross-compilation toolchain and vcpkg cross-triplet:
   ```bash
   cmake -B build -S . -G Ninja \
       -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_TOOLCHAIN_FILE="/path/to/clang-cl-msvc.cmake" \
       -DCMAKE_PREFIX_PATH="/path/to/vcpkg/installed/x64-windows-cross"
   cmake --build build --config Release
   ```
3. Verify DLL architecture and export symbols:
   ```bash
   llvm-objdump -p build/DovaJump.dll | grep SKSEPlugin
   ```

---

## Packaging Releases

To generate installable zip archives for Mod Organizer 2 and Vortex:

### Using the Shell Wrapper (Linux / macOS)
```bash
./package-release.sh
```

Flags:
- `--skip-build`: Package existing `build/DovaJump.dll` without re-running CMake.
- `--ng-only`: Build only `DovaJump-NG-Patch-v1.1.0-AE17104.zip`.
- `--aio-only`: Build only `DovaJump-AllInOne-v1.1.0-AE17104.zip`.

### Using Portable Python (Windows / Linux / macOS)
```cmd
python scripts/package-release.py
```

Output archives are generated in `dist/`:
- `dist/DovaJump-NG-Patch-v1.1.0-AE17104.zip`
- `dist/DovaJump-AllInOne-v1.1.0-AE17104.zip`

---

## Credits & License

- **Ignandr9**: Original creator of [Dova Jump](https://www.nexusmods.com/skyrimspecialedition/mods/125550).
- **BlessingS91**: Creator of the [Dova Jump NG Patch](https://www.nexusmods.com/skyrimspecialedition/mods/190782) and wall-jump overhaul.
- **meh321**: Creator of [Better Jumping](https://www.nexusmods.com/skyrimspecialedition/mods/18967) for foundational jump hooking concepts.
- **alandtse & CommonLibSSE-NG Contributors**: Cross-runtime SKSE interface library.
- **kxgcayh**: Skyrim AE 1.7.99/1.7.104 vtable shift resolution, AddressLibraryV5 pre-load gate compliance, cross-compilation pipeline, and release packaging.

This project is released under the MIT License. See [LICENSE](LICENSE) for details.
