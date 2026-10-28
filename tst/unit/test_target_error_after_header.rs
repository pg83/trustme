//@ compile-flags: --target=$DIR/support/targets/after_header.toml --print cfg
//@ input: support/targets/after_header.toml
//@ compile-fail: Unexpected token after block block
//@ crate-type: lib
