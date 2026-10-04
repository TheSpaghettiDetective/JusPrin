#!/usr/bin/env python3
"""Verify the checked-in functional icons against an unpacked lucide-static release.

Usage: python3 verify_lucide.py /path/to/unpacked/lucide-static/package
"""

import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    here = Path(__file__).resolve().parent
    package = Path(sys.argv[1]).resolve()
    manifest = json.loads((here / "lucide-map.json").read_text())
    package_manifest = json.loads((package / "package.json").read_text())
    expected = f"{manifest['package']}@{manifest['version']}"
    actual = f"{package_manifest['name']}@{package_manifest['version']}"
    if actual != expected:
        print(f"Expected {expected}, got {actual}", file=sys.stderr)
        return 1
    mismatches = []
    known = set(manifest["icons"]) | {"agent-bot"}
    for extra in sorted({path.stem for path in here.glob("*.svg")} - known):
        mismatches.append(f"{extra}.svg has no provenance mapping")
    for local, upstream in manifest["icons"].items():
        if (here / f"{local}.svg").read_bytes() != (package / "icons" / f"{upstream}.svg").read_bytes():
            mismatches.append(f"{local}.svg != {upstream}.svg")
    if (here / "LUCIDE-LICENSE.txt").read_bytes() != (package / "LICENSE").read_bytes():
        mismatches.append("LUCIDE-LICENSE.txt != LICENSE")
    if mismatches:
        print("\n".join(mismatches), file=sys.stderr)
        return 1
    print(f"Verified {len(manifest['icons'])} SVGs and license against {expected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
