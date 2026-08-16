#!/usr/bin/env python3
"""resvg smoke test: render a bundled SVG with the freshly built binary and
assert the PNG is well-formed and non-empty. This is the project's "test run"
in the tst/ graph — proof that the trustme-built binary actually works."""
import struct
import subprocess
import sys
import tempfile
import os

SVG = b"""<svg xmlns="http://www.w3.org/2000/svg" width="200" height="120">
  <rect x="10" y="10" width="80" height="60" fill="#c33" rx="8"/>
  <circle cx="150" cy="40" r="30" fill="#36c" opacity="0.8"/>
  <path d="M 20 100 Q 100 60 180 100" stroke="#282" stroke-width="4" fill="none"/>
</svg>
"""


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: run.py <resvg-binary>", file=sys.stderr)
        return 2
    resvg = sys.argv[1]
    with tempfile.TemporaryDirectory() as tmp:
        svg = os.path.join(tmp, "scene.svg")
        png = os.path.join(tmp, "scene.png")
        with open(svg, "wb") as fh:
            fh.write(SVG)
        proc = subprocess.run([resvg, svg, png], capture_output=True)
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr.decode(errors="replace"))
            print("resvg exited", proc.returncode, file=sys.stderr)
            return 1
        with open(png, "rb") as fh:
            data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        print("output is not a PNG", file=sys.stderr)
        return 1
    w, h = struct.unpack(">II", data[16:24])
    if (w, h) != (200, 120):
        print(f"unexpected PNG size {w}x{h}", file=sys.stderr)
        return 1
    # A blank canvas would compress far smaller; the drawn scene is well above this.
    if len(data) < 1000:
        print(f"PNG suspiciously small ({len(data)} bytes)", file=sys.stderr)
        return 1
    print(f"resvg ok: rendered {w}x{h} PNG, {len(data)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
