#!/usr/bin/env python3
"""
DovaJump Release Packager
Generates installable mod manager archives (Vortex / Mod Organizer 2) for DovaJump.

Zero external dependencies (uses standard Python 3.8+ libraries).
"""

import argparse
import hashlib
import os
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

VERSION = "1.1.0"
VERSION_STR = "1.1.0.0"

META_INI_NG = f"""[General]
gameName=SkyrimSE
modid=190782
version={VERSION_STR}
newestVersion={VERSION_STR}
category=34
notes=Dova Jump NG Patch ported and verified for Skyrim AE 1.7.104
author=BlessingS91 & Ignandr9
installedFiles\\size=0
"""

META_INI_AIO = f"""[General]
gameName=SkyrimSE
modid=125550
version={VERSION_STR}
newestVersion={VERSION_STR}
category=34
notes=Dova Jump complete standalone edition with NG double jump & AE 1.7.104 fix
author=Ignandr9 & BlessingS91
installedFiles\\size=0
"""


def sha256_file(filepath: Path) -> str:
    hasher = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            hasher.update(chunk)
    return hasher.hexdigest()


def find_dll(repo_root: Path, explicit_path: str = None) -> Path:
    if explicit_path:
        p = Path(explicit_path)
        if p.is_file():
            return p
        raise FileNotFoundError(f"Specified DLL does not exist: {explicit_path}")

    candidates = [
        repo_root / "build" / "DovaJump.dll",
        repo_root / "build" / "Release" / "DovaJump.dll",
        repo_root / "out" / "build" / "x64-Release" / "DovaJump.dll",
        repo_root / "cmake-build-release" / "DovaJump.dll",
    ]
    for c in candidates:
        if c.is_file():
            return c

    raise FileNotFoundError(
        "Could not find DovaJump.dll in standard build locations. "
        "Build the project first or provide --dll path."
    )


def package_ng(dll_path: Path, ini_path: Path, dist_dir: Path) -> Path:
    archive_name = f"DovaJump-NG-Patch-v{VERSION}-AE17104.zip"
    archive_path = dist_dir / archive_name

    with tempfile.TemporaryDirectory() as tmpdir:
        stage = Path(tmpdir)
        plugins_dir = stage / "SKSE" / "Plugins"
        plugins_dir.mkdir(parents=True, exist_ok=True)

        shutil.copy2(dll_path, plugins_dir / "DovaJump.dll")
        if ini_path.is_file():
            shutil.copy2(ini_path, plugins_dir / "JumpRequirements.ini")

        with open(stage / "meta.ini", "w", encoding="utf-8") as f:
            f.write(META_INI_NG)

        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, _, files in os.walk(stage):
                for file in sorted(files):
                    abs_file = Path(root) / file
                    arcname = abs_file.relative_to(stage)
                    zf.write(abs_file, arcname)

    return archive_path


def package_aio(dll_path: Path, ini_path: Path, base_mod_dir: Path, dist_dir: Path) -> Path:
    archive_name = f"DovaJump-AllInOne-v{VERSION}-AE17104.zip"
    archive_path = dist_dir / archive_name

    with tempfile.TemporaryDirectory() as tmpdir:
        stage = Path(tmpdir)

        # Copy base mod assets
        for item in ["meshes", "Scripts", "Source"]:
            src = base_mod_dir / item
            if src.is_dir():
                shutil.copytree(src, stage / item)

        esp = base_mod_dir / "DovaJumpMCM.esp"
        if esp.is_file():
            shutil.copy2(esp, stage / "DovaJumpMCM.esp")

        plugins_dir = stage / "SKSE" / "Plugins"
        plugins_dir.mkdir(parents=True, exist_ok=True)

        shutil.copy2(dll_path, plugins_dir / "DovaJump.dll")
        if ini_path.is_file():
            shutil.copy2(ini_path, plugins_dir / "JumpRequirements.ini")

        with open(stage / "meta.ini", "w", encoding="utf-8") as f:
            f.write(META_INI_AIO)

        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, _, files in os.walk(stage):
                for file in sorted(files):
                    abs_file = Path(root) / file
                    arcname = abs_file.relative_to(stage)
                    zf.write(abs_file, arcname)

    return archive_path


def main():
    parser = argparse.ArgumentParser(description="Package DovaJump releases for mod managers")
    parser.add_argument("--dll", help="Path to compiled DovaJump.dll")
    parser.add_argument("--ini", help="Path to JumpRequirements.ini")
    parser.add_argument("--base-mod", help="Path to base Dova Jump mod directory (meshes, MCM esp)")
    parser.add_argument("--dist", help="Output directory for packaged archives", default="dist")
    parser.add_argument("--ng-only", action="store_true", help="Only build NG patch archive")
    parser.add_argument("--aio-only", action="store_true", help="Only build All-In-One archive")

    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    dist_dir = (repo_root / args.dist).resolve()
    dist_dir.mkdir(parents=True, exist_ok=True)

    dll_path = find_dll(repo_root, args.dll)
    print(f"Found DLL: {dll_path} ({dll_path.stat().st_size} bytes)")

    ini_path = Path(args.ini) if args.ini else (repo_root / "assets" / "SKSE" / "Plugins" / "JumpRequirements.ini")
    if not ini_path.is_file():
        # Fallback to sibling NG patch directory if running in development workspace
        sibling_ini = repo_root.parent / "Dova Jump NG patch" / "SKSE" / "Plugins" / "JumpRequirements.ini"
        if sibling_ini.is_file():
            ini_path = sibling_ini

    base_mod_dir = Path(args.base_mod) if args.base_mod else (repo_root.parent / "Dova Jump")
    if not base_mod_dir.is_dir():
        local_base = repo_root / "assets" / "base_mod"
        if local_base.is_dir():
            base_mod_dir = local_base

    print("==================================================")
    print("DovaJump Mod Manager Release Packager")
    print("==================================================")

    created_archives = []

    if not args.aio_only:
        print("[1/2] Packaging NG Patch archive...")
        ng_zip = package_ng(dll_path, ini_path, dist_dir)
        created_archives.append(ng_zip)
        print(f"      Created: {ng_zip.name}")

    if not args.ng_only and base_mod_dir and base_mod_dir.is_dir():
        print("[2/2] Packaging All-In-One archive...")
        aio_zip = package_aio(dll_path, ini_path, base_mod_dir, dist_dir)
        created_archives.append(aio_zip)
        print(f"      Created: {aio_zip.name}")
    elif not args.ng_only:
        print("[2/2] Skipping All-In-One archive (base mod directory not found)")

    print("\nRelease Archives Summary:")
    print("--------------------------------------------------")
    for arc in created_archives:
        checksum = sha256_file(arc)
        size_kb = arc.stat().st_size / 1024
        print(f"Archive:  {arc.name} ({size_kb:.1f} KB)")
        print(f"SHA-256:  {checksum}")
        print("Contents:")
        with zipfile.ZipFile(arc, "r") as zf:
            for info in zf.infolist()[:10]:
                print(f"  - {info.filename}")
            if len(zf.infolist()) > 10:
                print(f"  ... ({len(zf.infolist()) - 10} more files)")
        print("--------------------------------------------------")

    print("Packaging complete.")


if __name__ == "__main__":
    main()
