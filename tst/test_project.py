#!/usr/bin/env python3
"""Run a pinned library project's native Cargo tests with the trustme toolchain.

    test_project.py <src.tar> <vendor.tar.zst> <libstd.tar> <manifest-subdir>
                    [cargo-test-args...]

`--xfail <test>` marks one test of a harness as an expected failure,
`--xfail-target <target>` a whole test target (a target with `harness = false`
has no test names to skip). Either way the code still has to compile and the
expected failure still has to fail.

Environment: RUSTC, CARGO, CC, BUILD_JOBS.
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lib  # noqa: E402

LIBRARY_KINDS = {"lib", "rlib", "dylib", "cdylib", "staticlib", "proc-macro"}


def split_test_args(args: list[str]) -> tuple[list[str], list[str], list[str], list[str]]:
    cargo_args = []
    harness_args = []
    xfails = []
    xfail_targets = []
    options = {
        "--xfail": (xfails, "test name"),
        "--xfail-target": (xfail_targets, "test target"),
    }
    index = 0
    while index < len(args):
        arg = args[index]
        if arg == "--":
            harness_args = args[index + 1:]
            break
        option, equals, name = arg.partition("=")
        if option in options:
            names, kind = options[option]
            if not equals:
                index += 1
                if index == len(args):
                    raise RuntimeError(f"{option} requires a {kind}")
                name = args[index]
            if not name:
                raise RuntimeError(f"{option} requires a {kind}")
            names.append(name)
        else:
            cargo_args.append(arg)
        index += 1
    return cargo_args, harness_args, xfails, xfail_targets


def package_targets(cargo: str, manifest: str, *, cwd: str, env: dict) -> list[dict]:
    """The targets cargo reads out of the manifest under test."""
    metadata = subprocess.run(
        [cargo, "metadata", "--no-deps", "--format-version", "1",
         "--manifest-path", manifest],
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        text=True,
        check=True,
    )
    wanted = os.path.abspath(manifest)
    for package in json.loads(metadata.stdout)["packages"]:
        if os.path.abspath(package["manifest_path"]) == wanted:
            return package["targets"]
    raise RuntimeError(f"no package owns the manifest: {manifest}")


def target_selectors(targets: list[dict], excluded: list[str]) -> list[str]:
    """Name the targets `cargo test` runs by default, minus the excluded ones."""
    unseen = set(excluded)
    selectors = []
    for target in targets:
        kinds = target["kind"]
        name = target["name"]
        if not target["test"]:
            continue
        if "test" in kinds:
            if name in unseen:
                unseen.discard(name)
            else:
                selectors += ["--test", name]
        elif "bin" in kinds:
            selectors += ["--bin", name]
        elif LIBRARY_KINDS.intersection(kinds):
            selectors.append("--lib")
    if unseen:
        raise RuntimeError(f"no such test target: {', '.join(sorted(unseen))}")
    return selectors


def run_tests(
    command: list[str], test_args: list[str], *, cwd: str, env: dict, manifest: str
) -> None:
    cargo_args, harness_args, xfails, xfail_targets = split_test_args(test_args)
    if not xfails and not xfail_targets:
        lib.run([*command, *test_args], cwd=cwd, env=env)
        return

    # Compile every test before allowing an expected runtime failure to pass.
    lib.run([*command, *cargo_args, "--no-run"], cwd=cwd, env=env)

    selectors = []
    if xfail_targets:
        targets = package_targets(command[0], manifest, cwd=cwd, env=env)
        selectors = target_selectors(targets, xfail_targets)

    skips = [argument for name in xfails for argument in ("--skip", name)]
    # An empty selection means every target of the package is an expected
    # failure, so the main run would have nothing left to select.
    if selectors or not xfail_targets:
        lib.run(
            [*command, *cargo_args, *selectors, "--", *harness_args, *skips],
            cwd=cwd,
            env=env,
        )

    # The selection also keeps an expected target failure out of the runs
    # below, where it would hide a test that started passing again.
    for name in xfails:
        result = subprocess.run(
            [*command, *cargo_args, *selectors, "--", name, "--exact", *harness_args],
            cwd=cwd,
            env=env,
            check=False,
        )
        if result.returncode == 0:
            raise RuntimeError(f"xfail unexpectedly passed: {name}")
        lib.log(f"[xfail] {name}")

    forwarded = ["--", *harness_args] if harness_args else []
    for name in xfail_targets:
        result = subprocess.run(
            [*command, *cargo_args, "--test", name, *forwarded],
            cwd=cwd,
            env=env,
            check=False,
        )
        if result.returncode == 0:
            raise RuntimeError(f"xfail target unexpectedly passed: {name}")
        lib.log(f"[xfail-target] {name}")


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
        env.setdefault("RUST_MIN_STACK", str(64 * 1024 * 1024))

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
        run_tests(
            command,
            test_args,
            cwd=os.path.join(src, subdir),
            env=env,
            manifest=manifest,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
