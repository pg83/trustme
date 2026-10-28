//@ compile-flags: --target=$DIR/support/targets/inline_separator.toml --print cfg
//@ input: support/targets/inline_separator.toml
//@ compile-fail: Unexpected token in TOML file after composite entry
//@ crate-type: lib
