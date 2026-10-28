//@ compile-flags: --target=$DIR/support/targets/array_ident.toml --print cfg
//@ input: support/targets/array_ident.toml
//@ compile-fail: Unexpected identifier in array value position
//@ crate-type: lib
