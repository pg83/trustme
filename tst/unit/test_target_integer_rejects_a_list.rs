//@ compile-flags: --target=$DIR/support/targets/wrong_integer_type.toml --print cfg
//@ input: support/targets/wrong_integer_type.toml
//@ compile-fail: expected integer, got list
fn main() {}
