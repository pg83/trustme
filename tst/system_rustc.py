#!/usr/bin/env python3
"""Build-graph helpers for running the Rust corpus with an external rustc."""

import os
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile


def resolve_executable(value: str) -> str:
    path = shutil.which(value)
    if path is None:
        raise SystemExit(f"system executable not found: {value}")
    return os.path.realpath(path)


def write_launcher(compiler: str, linker: str, output: str) -> None:
    compiler = resolve_executable(compiler)
    linker = resolve_executable(linker)
    output_path = Path(output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        "#!/usr/bin/env python3\n"
        "import os\n"
        "import sys\n"
        f"compiler = {compiler!r}\n"
        f"linker = {linker!r}\n"
        "os.environ['RUSTC_BOOTSTRAP'] = '1'\n"
        "os.execv(compiler, [compiler, *sys.argv[1:], '-C', f'linker={linker}'])\n"
    )
    output_path.chmod(0o755)


def write_empty_libstd(output: str) -> None:
    output_path = Path(output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as work_text:
        release = Path(work_text) / "release"
        release.mkdir()
        with tarfile.open(output_path, "w") as archive:
            archive.add(release, arcname="./release")


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit(
            "usage: system_rustc.py launcher COMPILER LINKER OUTPUT | "
            "empty-libstd OUTPUT"
        )
    if sys.argv[1] == "launcher" and len(sys.argv) == 5:
        write_launcher(sys.argv[2], sys.argv[3], sys.argv[4])
        return 0
    if sys.argv[1] == "empty-libstd" and len(sys.argv) == 3:
        write_empty_libstd(sys.argv[2])
        return 0
    raise SystemExit("invalid system_rustc.py arguments")


if __name__ == "__main__":
    raise SystemExit(main())
