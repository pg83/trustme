# gccrs execute corpus

Source: <https://github.com/Rust-GCC/gccrs/tree/d273e1b6a25261b49983fc58471da3245b96928f/gcc/testsuite/rust/execute>

Revision: commit `d273e1b6a25261b49983fc58471da3245b96928f`.

`upstream/` contains the individual Rust sources from `execute/`, including
`torture/`, plus source-level module and `include!` inputs.  `cases.txt` lists
the 301 executable roots; support files are not separate build nodes.

Refresh from a temporary sparse checkout with:

```sh
tests/gccrs/import.py /tmp/gccrs
```
