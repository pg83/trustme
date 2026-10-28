//@ compile-flags: --target=$DIR/support/targets/entry_separator.toml --print cfg
//@ input: support/targets/entry_separator.toml
//@ compile-fail: Unexpected token in TOML file after entry
//@ crate-type: lib
