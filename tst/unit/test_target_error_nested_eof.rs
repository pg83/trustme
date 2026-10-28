//@ compile-flags: --target=$DIR/support/targets/nested_eof.toml --print cfg
//@ input: support/targets/nested_eof.toml
//@ compile-fail: Unexpected EOF in nested array/table value
//@ crate-type: lib
