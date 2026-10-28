//@ compile-flags: --target=$DIR/support/targets/duplicate_arch.toml --print cfg
//@ input: support/targets/duplicate_arch.toml
//@ compile-fail: Architecture already specified
//@ crate-type: lib
