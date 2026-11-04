//@ input: support/targets/backend_field_deep_value.toml
//@ compile-flags: --target=$DIR/support/targets/backend_field_deep_value.toml
//@ compile-fail: Unexpected sub-node
//@ compile-fail-exit-code: 1
fn main() {}
