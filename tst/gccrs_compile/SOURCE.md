# gccrs positive compile corpus

Source: <https://github.com/Rust-GCC/gccrs/tree/d273e1b6a25261b49983fc58471da3245b96928f/gcc/testsuite/rust/compile>

Revision: commit `d273e1b6a25261b49983fc58471da3245b96928f`.

The importer selects sources without `dg-error`, `dg-warning`, `dg-message`, or
`dg-bogus`: their upstream invariant is successful compilation.  It excludes
xfail/skip cases and frontend-only (`-fsyntax-only` or pre-compilation
`-frust-compile-until`) cases because this compiler has no equivalent partial
pipeline mode.  The retained 582 source files are compiled as libraries;
build nodes contain at most ten files.

Refresh from a temporary sparse checkout with:

```sh
tst/gccrs_compile/import.py /tmp/gccrs
```
