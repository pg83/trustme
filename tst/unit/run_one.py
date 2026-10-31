#!/ usr / bin / env python3
"""Compile one tst/unit/test_*.rs against a prebuilt libstd, then write a stamp.
Each unit test is its own graph node — a self-contained regression for one
compiler fix. Binaries and test harnesses must exit 0; `//@ crate-type: lib`
units are compile-only, as are units with `//@ compile-only`.
`reject-flags` checks own/native diagnostics before a successful normal run;
`stdout-flags` checks CLI query output, and `compile-variant` also runs the
program with the given flags. `$OUT` names each temporary output path.

    run_one.py <test.rs> <libstd.tar> <stamp>

Environment: RUSTC, CC.
"""
import hashlib
import os
import re
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    src = os.path.abspath(sys.argv[1])
    libstd_tar = os.path.abspath(sys.argv[2])
    stamp = os.path.abspath(sys.argv[3])
    rustc = lib.require_env("RUSTC")
    source_text = open(src, encoding="utf-8", errors="surrogateescape").read()
    system_rustc = os.environ.get("TRUSTME_SYSTEM_RUSTC") == "1"
    test_harness = "//@ test-harness" in source_text
    compile_only = bool(re.search(r"^//@\s*compile-only\s*$", source_text, re.MULTILINE))
    output_checks = re.findall(
        r"^//@\s*output-contains:\s*((?:_[1-3]_[a-z]+)?\.[a-z]+)\s+(.+)$", source_text, re.MULTILINE
    )
    if compile_only and test_harness:
        raise RuntimeError("compile-only units cannot be test harnesses")
    compile_fail_match = re.search(
        r"^//@\s*compile-fail:\s*(.+)$", source_text, re.MULTILINE
    )
    edition_match = re.search(r"^//@\s*edition:\s*(\d+)", source_text, re.MULTILINE)
    edition = edition_match.group(1) if edition_match else "2021"
    compile_flags_match = re.search(
        r"^//@\s*compile-flags:\s*(.*)$", source_text, re.MULTILINE
    )
    compile_flags = (
        shlex.split(compile_flags_match.group(1)) if compile_flags_match else []
    )
    compile_flags = [flag.replace("$DIR", os.path.dirname(src)) for flag in compile_flags]
    reject_options = re.findall(
        r"^//@\s*reject-flags:\s*(.*?)\s*=>\s*(.+?)\s*\|\|\s*(.+?)\s*$",
        source_text, re.MULTILINE,
    )
    if len(reject_options) != len(re.findall(r"^//@\s*reject-flags:", source_text, re.MULTILINE)):
        raise RuntimeError("reject-flags requires FLAGS => OWN_DIAGNOSTIC || NATIVE_DIAGNOSTIC")
    compile_variants = [
        shlex.split(flags) for flags in re.findall(
            r"^//@\s*compile-variant:\s*(.+)$", source_text, re.MULTILINE
        )
    ]
    stdout_options = re.findall(
        r"^//@\s*stdout-flags:\s*(.*?)\s*=>\s*(.+?)\s*$", source_text, re.MULTILINE
    )
    if len(stdout_options) != len(re.findall(r"^//@\s*stdout-flags:", source_text, re.MULTILINE)):
        raise RuntimeError("stdout-flags requires FLAGS => EXPECTED_OUTPUT")
    if compile_fail_match and (reject_options or compile_variants or stdout_options):
        raise RuntimeError("compile-fail cannot be combined with CLI checks or compile variants")
    chaos_match = re.search(r"^//@\s*chaos:\s*(\S+)\s*$", source_text, re.MULTILINE)
    crate_type_match = re.search(
        r"^//@\s*crate-type:\s*([a-z_]+)\s*$", source_text, re.MULTILINE
    )
    crate_type = crate_type_match.group(1) if crate_type_match else "bin"
    if crate_type not in {"bin", "cdylib", "lib"}:
        raise RuntimeError(f"unsupported unit crate type: {crate_type}")
    rust_lib_dependencies = "//@ rust-lib-dev-dependencies" in source_text
    aux_builds = re.findall(r"^//@\s*aux-build:\s*(\S+)\s*$", source_text, re.MULTILINE)
    aux_search = {
        os.path.splitext(os.path.basename(aux))[0]
        for aux in re.findall(r"^//@\s*aux-search:\s*(\S+)\s*$", source_text, re.MULTILINE)
    }
    aux_directories = dict(re.findall(
        r"^//@\s*aux-output-dir:\s*(\S+?)=(.+?)\s*$", source_text, re.MULTILINE
    ))
    for directory in aux_directories.values():
        if os.path.isabs(directory) or ".." in directory.split("/"):
            raise RuntimeError("aux-output-dir must stay inside the temporary work directory")
    proc_macro_aux_builds = re.findall(
        r"^//@\s*proc-macro-aux-build:\s*(\S+)\s*$", source_text, re.MULTILINE
    )

    with lib.workdir() as work:
        env = dict(os.environ)
        env.pop("TRUSTME_SYSTEM_RUSTC", None)
        env.pop("TRUSTME_CHAOS", None)
        env.setdefault("CC", "cc")

        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        dependency_args = []
        if rust_lib_dependencies:
            dependencies = lib.untar(
                lib.require_env("RUST_LIB_DEPENDENCIES"),
                os.path.join(work, "rust-lib-dependencies"),
            )
            dependency_args = lib.extern_rlib_args(
                dependencies,
                ["rand", "rand_xorshift"],
            )
        # `//@ proc-macro-aux-build: name.rs`: an aux crate built as a macro host -
        # tst/unit/aux/name.rs built as a proc-macro crate. Ours writes the host
        # as an executable with its metadata beside it, so the unit also needs
        # `--proc-macro name=<host>`; upstream's host is a shared object that
        # `--extern` alone names.
        for aux in proc_macro_aux_builds:
            aux_src = os.path.join(os.path.dirname(src), "aux", aux)
            aux_name = os.path.splitext(os.path.basename(aux))[0]
            with open(aux_src) as aux_file:
                aux_edition_match = re.search(r"^//@\s*edition:\s*(\d+)", aux_file.read(), re.MULTILINE)
            aux_edition = aux_edition_match.group(1) if aux_edition_match else edition
            aux_host = os.path.join(
                work, f"lib{aux_name}.so" if system_rustc else f"lib{aux_name}-plugin"
            )
            lib.run(
                [rustc, aux_src, "-L", os.path.join(libstd, "release"),
                 "--crate-type", "proc-macro", "--crate-name", aux_name,
                 "-o", aux_host, "--edition", aux_edition],
                env=env,
            )
            if system_rustc:
                dependency_args.extend(("--extern", f"{aux_name}={aux_host}"))
            else:
                dependency_args.extend((
                    "--extern", f"{aux_name}={aux_host}.rlib",
                    "--proc-macro", f"{aux_name}={aux_host}",
                ))
        # `//@ aux-build: name.rs`: tst/unit/aux/name.rs built as an rlib the unit
        # links with `--extern name=...` - named or not, as a Cargo dependency is.
        for aux in aux_builds:
            aux_src = os.path.join(os.path.dirname(src), "aux", aux)
            aux_name = os.path.splitext(os.path.basename(aux))[0]
            suffix = ""
            if aux_name in aux_search:
                with open(aux_src, "rb") as aux_file:
                    suffix = "-" + hashlib.file_digest(aux_file, "sha256").hexdigest()[:16]
            aux_directory = os.path.join(work, aux_directories.get(aux, ""))
            os.makedirs(aux_directory, exist_ok=True)
            aux_rlib = os.path.join(aux_directory, f"lib{aux_name}{suffix}.rlib")
            # an aux crate may name its own edition (`//@ edition: 2018` in the aux)
            with open(aux_src) as aux_file:
                aux_edition_match = re.search(r"^//@\s*edition:\s*(\d+)", aux_file.read(), re.MULTILINE)
            aux_edition = aux_edition_match.group(1) if aux_edition_match else edition
            lib.run(
                [rustc, aux_src, "-L", os.path.join(libstd, "release"),
                 "--crate-type", "rlib", "--crate-name", aux_name, "-o", aux_rlib,
                 "--edition", aux_edition, *dependency_args],
                env=env,
            )
            if aux_name in aux_search:
                dependency_args.extend(("-L", aux_directory, "--extern", aux_name))
            else:
                dependency_args.extend(("--extern", f"{aux_name}={aux_rlib}"))
        binary = os.path.join(work, "t")
        base_flags = [flag.replace("$OUT", binary) for flag in compile_flags]
        if chaos_match:
            rustc = lib.require_env("RUSTC_CHAOS")
            env["TRUSTME_CHAOS"] = chaos_match.group(1)
        mode = ["--test"] if test_harness else ["--crate-type", crate_type]
        for flags, own_diagnostic, native_diagnostic in reject_options:
            extra = [flag.replace("$DIR", os.path.dirname(src)).replace("$OUT", binary)
                     for flag in shlex.split(flags)]
            rejected = subprocess.run(
                [rustc, src, "-L", os.path.join(libstd, "release"), "-o", binary,
                 *dependency_args, *base_flags, *extra],
                env=dict(env, TRUSTME_CLI_EXACT_ARGS="1"),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
                check=False,
            )
            expected = (native_diagnostic if system_rustc else own_diagnostic).encode()
            diagnostic = rejected.stdout + rejected.stderr
            if (rejected.returncode == 0 or expected not in diagnostic
                    or b"BUG:" in diagnostic or b"internal compiler error" in diagnostic):
                sys.stderr.buffer.write(diagnostic[-16384:])
                raise RuntimeError(f"option rejection failed: {extra!r}, expected {expected!r}")
            print(f"option rejection: {extra!r}: {expected.decode()}", flush=True)
        for flags, expected in stdout_options:
            extra = [flag.replace("$DIR", os.path.dirname(src)).replace("$OUT", binary)
                     for flag in shlex.split(flags)]
            inspected = subprocess.run(
                [rustc, src, "-L", os.path.join(libstd, "release"), "-o", binary,
                 *dependency_args, *base_flags, *extra],
                env=dict(env, TRUSTME_CLI_EXACT_ARGS="1"),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=False,
            )
            if inspected.returncode or expected.encode() not in inspected.stdout:
                sys.stderr.buffer.write((inspected.stdout + inspected.stderr)[-16384:])
                raise RuntimeError(f"option output failed: {extra!r}, expected {expected!r}")
            print(f"option output: {extra!r}: {expected}", flush=True)
        command = lib.wrap_gdb(
            [rustc, src, "-L", os.path.join(libstd, "release"), "-o", binary,
             *mode, "--edition", edition, *dependency_args, *base_flags]
        )
        expected_failure = compile_fail_match is not None
        if expected_failure:
            result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, check=False)
            if result.returncode == 0:
                raise RuntimeError("compiler unexpectedly accepted compile-fail unit")
            expected = compile_fail_match.group(1).encode() if compile_fail_match else b""
            if expected and not system_rustc and expected not in result.stderr:
                sys.stdout.buffer.write(result.stdout)
                sys.stderr.buffer.write(result.stderr)
                raise RuntimeError(
                    f"compile-fail unit did not emit expected diagnostic: {expected!r}"
                )
            if chaos_match:
                for rule in chaos_match.group(1).split(","):
                    marker = ("CHAOS " + rule.split("@", 1)[0] + "\n").encode()
                    if marker not in result.stderr:
                        raise RuntimeError(f"compiler did not exercise requested fault: {rule}")
        else:
            lib.run(command, env=env)
        if expected_failure:
            os.makedirs(os.path.dirname(stamp), exist_ok=True)
            open(stamp, "w").close()
            return 0
        for suffix, expected in output_checks:
            with open(binary + suffix, encoding="utf-8") as output:
                if expected not in output.read():
                    raise RuntimeError(f"compiler output {suffix} does not contain {expected!r}")
        if test_harness:
            listing = subprocess.run([binary, "--list"], env=env,
                                     stdout=subprocess.PIPE, timeout=60,
                                     check=True)
            if b": test" not in listing.stdout:
                raise RuntimeError("test harness contains no tests")
        if test_harness or (crate_type == "bin" and not compile_only):
            lib.run([binary], env=env, timeout=60)
        for index, flags in enumerate(compile_variants):
            variant = f"{binary}-variant{index}"
            extra = [flag.replace("$DIR", os.path.dirname(src)).replace("$OUT", variant)
                     for flag in flags]
            variant_flags = [flag.replace("$OUT", variant) for flag in compile_flags]
            lib.run(lib.wrap_gdb(
                [rustc, src, "-L", os.path.join(libstd, "release"), "-o", variant,
                 *mode, "--edition", edition, *dependency_args, *variant_flags, *extra]
            ), env=env)
            for suffix, expected in output_checks:
                with open(variant + suffix, encoding="utf-8") as output:
                    if expected not in output.read():
                        raise RuntimeError(f"compiler output {suffix} does not contain {expected!r}")
            if test_harness or (crate_type == "bin" and not compile_only):
                lib.run([variant], env=env, timeout=60)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
