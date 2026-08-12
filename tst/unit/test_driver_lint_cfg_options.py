#!/usr/bin/env python3
"""Check that rustc-compatible cfg and lint flags are accepted (checking is not performed)."""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def invoke(rustc: str, src: str, work: str, name: str, args: list[str]) -> subprocess.CompletedProcess[str]:
    env = dict(os.environ)
    return subprocess.run(
        lib.wrap_gdb([
            rustc,
            src,
            "--crate-name",
            "driver_lint_cfg",
            "--crate-type",
            "lib",
            "-o",
            os.path.join(work, name),
            "-Zstop-after=expand",
            *args,
        ]),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def expect_ok(result: subprocess.CompletedProcess[str], what: str) -> None:
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"{what} failed with exit {result.returncode}")


def expect_error(result: subprocess.CompletedProcess[str], what: str, needle: str) -> None:
    if result.returncode == 0:
        raise RuntimeError(f"{what} was accepted")
    if needle not in result.stderr:
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"{what} did not report {needle!r}")


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_driver_lint_cfg_options.py RUSTC INPUT_RS STAMP")

    rustc, src, stamp = map(os.path.abspath, sys.argv[1:])
    cfgs = ["--cfg=driver_flag", '--cfg', 'driver_value="active"']
    expected_cfgs = [
        "--check-cfg=cfg(driver_flag, driver_unknown)",
        "--check-cfg",
        'cfg(driver_value, values("active", "unexpected"))',
    ]

    with tempfile.TemporaryDirectory(prefix="mrustc-driver-lint-cfg-") as work:
        spellings = invoke(
            rustc,
            src,
            work,
            "spellings",
            [
                *cfgs,
                *expected_cfgs,
                "-Adead_code",
                "-A",
                "warnings",
                "-Wunused_variables",
                "-W",
                "nonstandard_style",
                "-Dunused",
                "-D",
                "unused_imports",
                "-Funsafe_code",
                "-F",
                "unsafe_op_in_unsafe_fn",
                "--force-warn=unused_assignments",
                "--force-warn",
                "unused_mut",
                "--cap-lints=warn",
                "-Zcheck-cfg-all-expected",
            ],
        )
        expect_ok(spellings, "rustc-compatible lint/cfg spellings")

        inactive = invoke(
            rustc,
            src,
            work,
            "inactive",
            [*cfgs, "-Dunexpected_cfgs"],
        )
        expect_ok(inactive, "unexpected_cfgs without --check-cfg")

        denied = invoke(
            rustc,
            src,
            work,
            "denied",
            [
                *cfgs,
                "--check-cfg=cfg(driver_flag)",
                '--check-cfg=cfg(driver_value, values("active"))',
                "-Dunexpected_cfgs",
            ],
        )
        expect_ok(denied, "-Dunexpected_cfgs accepted (cfg expectations are not checked)")

        allowed = invoke(
            rustc,
            src,
            work,
            "allowed",
            [
                *cfgs,
                "--check-cfg=cfg(driver_flag)",
                '--check-cfg=cfg(driver_value, values("active"))',
                "-Dwarnings",
                "-Aunexpected_cfgs",
            ],
        )
        expect_ok(allowed, "explicitly allowed unexpected cfg")

        capped = invoke(
            rustc,
            src,
            work,
            "capped",
            [
                *cfgs,
                "--check-cfg=cfg(driver_flag)",
                '--check-cfg=cfg(driver_value, values("active"))',
                "-Dunexpected_cfgs",
                "--cap-lints=allow",
            ],
        )
        expect_ok(capped, "cap-lints=allow")

        forced = invoke(
            rustc,
            src,
            work,
            "forced",
            [
                *cfgs,
                "--check-cfg=cfg(driver_flag)",
                '--check-cfg=cfg(driver_value, values("active"))',
                "--force-warn=unexpected_cfgs",
                "--cap-lints=allow",
            ],
        )
        expect_ok(forced, "force-warn unexpected cfg accepted")

        invalid_check = invoke(
            rustc,
            src,
            work,
            "invalid-check",
            [*cfgs, "--check-cfg=cfg(driver_value, values(active))"],
        )
        expect_ok(invalid_check, "--check-cfg accepted without validation")

        invalid_cap = invoke(
            rustc,
            src,
            work,
            "invalid-cap",
            [*cfgs, "--cap-lints=force-warn"],
        )
        expect_error(invalid_cap, "invalid --cap-lints level", "unknown lint level")

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
