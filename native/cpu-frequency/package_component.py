#!/usr/bin/env python3
"""Package built 1C Native API binaries into a manifest ZIP."""

from __future__ import annotations

import argparse
import re
import sys
import zipfile
from pathlib import Path
from xml.etree.ElementTree import Element, SubElement, tostring


TARGETS = {
    "windows-i386": ("Windows", "i386", ".dll", "CpuFrequencyProbe_Win32.dll"),
    "windows-x86_64": ("Windows", "x86_64", ".dll", "CpuFrequencyProbe_Win64.dll"),
    "linux-i386": ("Linux", "i386", ".so", "CpuFrequencyProbe_Lin32.so"),
    "linux-x86_64": ("Linux", "x86_64", ".so", "CpuFrequencyProbe_Lin64.so"),
    "linux-arm64": ("Linux", "ARM64", ".so", "CpuFrequencyProbe_LinARM64.so"),
    "linux-e2k": ("Linux", "E2K", ".so", "CpuFrequencyProbe_LinE2K.so"),
    "macos-x86_64": ("MacOS", "x86_64", ".dylib", "CpuFrequencyProbe_MacOS64.dylib"),
    "macos-arm64": ("MacOS", "ARM64", ".dylib", "CpuFrequencyProbe_MacOSARM64.dylib"),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd(), help="repository root")
    parser.add_argument("--output", type=Path, default=Path("build/native/CpuFrequencyProbe.zip"))
    parser.add_argument("--target", action="append", choices=sorted(TARGETS), required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    output = args.output if args.output.is_absolute() else root / args.output
    cmake = (root / "native/cpu-frequency/CMakeLists.txt").read_text(encoding="utf-8")
    version_match = re.search(r"project\(CpuFrequencyProbe VERSION ([0-9]+\.[0-9]+\.[0-9]+)", cmake)
    if version_match is None:
        print("Could not read CpuFrequencyProbe version from CMakeLists.txt", file=sys.stderr)
        return 2
    component_version = version_match.group(1)

    def versioned_filename(filename: str) -> str:
        path = Path(filename)
        version_suffix = component_version.replace(".", "_")
        return f"{path.stem}_v{version_suffix}{path.suffix}"

    binaries: list[tuple[str, Path]] = []
    for target in args.target:
        _, _, suffix, base_package_name = TARGETS[target]
        package_name = versioned_filename(base_package_name)
        binary = root / "build" / "native" / target / f"CpuFrequencyProbe{suffix}"
        if not binary.is_file():
            print(f"Missing built binary for {target}: {binary}", file=sys.stderr)
            return 2
        binaries.append((package_name, binary))

    manifest = Element("bundle", {"xmlns": "http://v8.1c.ru/8.2/addin/bundle", "name": "CpuFrequencyProbe"})
    for target in args.target:
        os_name, arch, _, base_package_name = TARGETS[target]
        package_name = versioned_filename(base_package_name)
        SubElement(manifest, "component", {"os": os_name, "path": package_name, "type": "native", "arch": arch})

    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr("MANIFEST.XML", b'<?xml version="1.0" encoding="UTF-8"?>\n' + tostring(manifest, encoding="utf-8"))
        for package_name, binary in binaries:
            archive.write(binary, package_name)
    print(f"Created {output} with {len(binaries)} platform binaries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
