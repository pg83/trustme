//@ input: support/targets/literal_quote_payload.toml
//@ compile-variant: --target=$DIR/support/targets/literal_quote_payload.toml --cfg quoted_target
fn main() {
    assert_eq!(i32::from_le_bytes([253, 255, 255, 255]), -3);
    #[cfg(quoted_target)]
    assert!(cfg!(target_env = "one'two''three\nend''"));
}
