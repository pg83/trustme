#!/usr/bin/env python3
"""Run a pinned library project's native Cargo tests with the trustme toolchain.

    test_project.py <src.tar> <vendor.tar.zst> <libstd.tar> <manifest-subdir>
                    [cargo-test-args...]

Environment: RUSTC, CARGO, CC, BUILD_JOBS.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lib  # noqa: E402


def split_test_args(args: list[str]) -> tuple[list[str], list[str], list[str]]:
    cargo_args = []
    harness_args = []
    xfails = []
    index = 0
    while index < len(args):
        arg = args[index]
        if arg == "--":
            harness_args = args[index + 1:]
            break
        if arg == "--xfail":
            index += 1
            if index == len(args):
                raise RuntimeError("--xfail requires a test name")
            xfails.append(args[index])
        elif arg.startswith("--xfail="):
            name = arg.split("=", 1)[1]
            if not name:
                raise RuntimeError("--xfail requires a test name")
            xfails.append(name)
        else:
            cargo_args.append(arg)
        index += 1
    return cargo_args, harness_args, xfails


def run_tests(command: list[str], test_args: list[str], *, cwd: str, env: dict) -> None:
    cargo_args, harness_args, xfails = split_test_args(test_args)
    if not xfails:
        lib.run([*command, *test_args], cwd=cwd, env=env)
        return

    # Compile every test before allowing an expected runtime failure to pass.
    lib.run([*command, *cargo_args, "--no-run"], cwd=cwd, env=env)

    skips = [argument for name in xfails for argument in ("--skip", name)]
    lib.run(
        [*command, *cargo_args, "--", *harness_args, *skips],
        cwd=cwd,
        env=env,
    )

    for name in xfails:
        result = subprocess.run(
            [*command, *cargo_args, "--", name, "--exact", *harness_args],
            cwd=cwd,
            env=env,
            check=False,
        )
        if result.returncode == 0:
            raise RuntimeError(f"xfail unexpectedly passed: {name}")
        lib.log(f"[xfail] {name}")


def main() -> int:
    src_tar = os.path.abspath(sys.argv[1])
    vendor_tar = os.path.abspath(sys.argv[2])
    libstd_tar = os.path.abspath(sys.argv[3])
    subdir = sys.argv[4]
    test_args = sys.argv[5:]
    cargo = lib.require_env("CARGO")
    jobs = lib.require_env("BUILD_JOBS")

    with lib.workdir() as work:
        env = dict(os.environ)
        env["TRUSTME_PATH"] = lib.trustme_link(work)
        env.setdefault("CC", "cc")

        src = lib.untar(src_tar, os.path.join(work, "src"))
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        vroot = lib.untar(vendor_tar, os.path.join(work, "vendor"), zstd=True)
        out = os.path.join(work, "out")
        os.makedirs(out, exist_ok=True)

        manifest = os.path.join(src, subdir, "Cargo.toml")
        lib.log(f"[test] {subdir}")
        command = [
            cargo, "test", "--locked", "-j", jobs,
            "--manifest-path", manifest,
            "--target-dir", out,
            "-Zvendor-dir=" + os.path.join(vroot, "vendor"),
            "-Zlib-search=" + os.path.join(libstd, "release"),
        ]
        run_tests(command, test_args, cwd=os.path.join(src, subdir), env=env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
