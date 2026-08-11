#!/usr/bin/env python3

import pathlib
import subprocess
import sys


def main() -> int:
    source_dir = pathlib.Path(sys.argv[1])
    binary = pathlib.Path(sys.argv[2])
    stamp = pathlib.Path(sys.argv[3])

    subprocess.run([binary], check=True)

    suffixes = {".h", ".cpp", ".inc"}
    offenders = []
    for path in source_dir.rglob("*"):
        if path.suffix in suffixes and "dynamic_cast" in path.read_text():
            offenders.append(path.relative_to(source_dir))
    if offenders:
        raise AssertionError(f"dynamic_cast returned in compiler sources: {offenders}")

    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
