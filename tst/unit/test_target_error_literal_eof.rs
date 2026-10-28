//@ compile-flags: --target=$DIR/support/targets/literal_eof.toml --print cfg
//@ input: support/targets/literal_eof.toml
//@ compile-fail: Unexpected EOF in single-quoted string
//@ crate-type: lib
