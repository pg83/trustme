//@ compile-flags: --target=$DIR/support/targets/quoted_eof.toml --print cfg
//@ input: support/targets/quoted_eof.toml
//@ compile-fail: Unexpected EOF in double-quoted string
//@ crate-type: lib
