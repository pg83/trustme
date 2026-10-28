//@ compile-flags: --target=$DIR/support/targets/windows.toml --print cfg
//@ input: support/targets/windows.toml
//@ compile-fail: Windows targets are not supported
//@ crate-type: lib
