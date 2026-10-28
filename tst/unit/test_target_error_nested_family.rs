//@ compile-flags: --target=$DIR/support/targets/nested_family.toml --print cfg
//@ input: support/targets/nested_family.toml
//@ compile-fail: Unexpected sub-node
//@ crate-type: lib
