//@ compile-flags: --target=$DIR/support/targets/missing_arch.toml --print cfg
//@ input: support/targets/missing_arch.toml
//@ compile-fail: Architecture not specified
//@ crate-type: lib
