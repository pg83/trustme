//@ compile-flags: --target=$DIR/support/targets/literal_multiline_eof.toml --print cfg
//@ input: support/targets/literal_multiline_eof.toml
//@ compile-fail: Unexpected EOF in triple-quoted string
//@ crate-type: lib
