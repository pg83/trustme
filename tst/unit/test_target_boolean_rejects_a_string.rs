//@ compile-flags: --target=$DIR/support/targets/wrong_boolean_type.toml --print cfg
//@ input: support/targets/wrong_boolean_type.toml
//@ compile-fail: expected boolean, got string
fn main() {}
