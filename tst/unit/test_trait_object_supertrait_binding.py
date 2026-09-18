#!/usr/bin/env python3
"""A supertrait binding a trait object repeats is not a missing vtable slot.

`trait IterTrait<'a, T: 'a>: Iterator<Item = &'a T>` pins `Item` for every
implementor, so the vtable struct reserves no type parameter for it and the
parent vtable field is filled from the declaration's own binding. The object
type still carries `Item = ..`, elaborated from the supertrait bound or written
by hand; upstream keeps no associated types in a vtable at all (`vtable_entries`,
rustc_trait_selection/src/traits/vtable.rs), so there is nothing to report.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


NOISE = (
    "no vtable type index",
    "references an associated type the trait does not declare",
)


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: test_trait_object_supertrait_binding.py RUSTC INPUT_RS LIBSTD_TAR STAMP"
        )

    rustc, src, libstd_tar, stamp = map(os.path.abspath, sys.argv[1:])

    with lib.workdir() as work:
        env = dict(os.environ)
        env.setdefault("CC", "cc")
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        binary = os.path.join(work, "trait_object_supertrait_binding")

        result = subprocess.run(
            lib.wrap_gdb([
                rustc, src, "-L", os.path.join(libstd, "release"),
                "--crate-type", "bin", "--edition", "2021", "-o", binary,
            ]),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=600,
            check=False,
        )
        if result.returncode != 0:
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise RuntimeError("trait object over a supertrait-bound iterator did not compile")
        for needle in NOISE:
            if needle in result.stderr:
                sys.stderr.write(result.stderr)
                raise RuntimeError(f"a pinned supertrait binding reported {needle!r}")

        lib.run([binary], env=env, timeout=60)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
