//@ input: support/targets/basic_quote_payload.toml
//@ input: support/targets/basic_escaped_payload.toml
//@ input: support/targets/multiline_escaped_payload.toml
//@ compile-variant: --target=$DIR/support/targets/basic_quote_payload.toml --cfg quoted_target
//@ compile-variant: --target=$DIR/support/targets/basic_escaped_payload.toml --cfg escaped_target
//@ compile-variant: --target=$DIR/support/targets/multiline_escaped_payload.toml --cfg escaped_multiline_target
fn main() {
    assert_eq!(23u64.rotate_left(8), 5888);
    #[cfg(quoted_target)]
    assert!(cfg!(target_env = "\"one\"two\"\"three\"\""));
    #[cfg(escaped_target)]
    assert!(cfg!(target_env = "one\\two\"three"));
    #[cfg(escaped_multiline_target)]
    assert!(cfg!(target_env = "one\nsecond\"wordthird"));
}
