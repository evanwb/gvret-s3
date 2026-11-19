#!/usr/bin/env python3
"""Stage a GitHub Pages installer using the same merged binary as CI's artifact."""
import argparse
import json
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--version", required=True, help="Git commit built into the firmware")
    parser.add_argument("--output", type=Path, default=Path("_site"))
    args = parser.parse_args()
    if not args.firmware.is_file():
        parser.error("Build the merged factory firmware before staging the installer.")

    source = Path(__file__).resolve().parent.parent / "web"
    shutil.copytree(source, args.output, dirs_exist_ok=True)
    shutil.copyfile(args.firmware, args.output / "gvret-s3-factory.bin")
    manifest_file = args.output / "manifest.json"
    manifest = json.loads(manifest_file.read_text())
    manifest["version"] = args.version
    manifest_file.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Staged installer and firmware {args.version[:12]} in {args.output}")


if __name__ == "__main__":
    main()
