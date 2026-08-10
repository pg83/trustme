# The Rust Reference code-fence corpus

Source: <https://github.com/rust-lang/reference>

Revision: commit `603630bdf2da97723f28899807c62591f312dd97`.

The importer extracts each Rust code fence, applies rustdoc's hidden-line and
implicit-`main` conventions, then verifies its documented mode with the
official Rust 1.90 compiler.  The 544 retained fences comprise 407 normal
runtime programs, one `should_panic` program, 22 `no_run` compile-success
programs, and 114 intentional `compile_fail` programs.  Files remain separate
under `upstream/`; build nodes run at most ten fences.

Refresh from a temporary checkout and an official Rust 1.90 toolchain with:

```sh
tst/rust_reference/import.py /tmp/reference /tmp/rust-1.90/bin/rustc
```
