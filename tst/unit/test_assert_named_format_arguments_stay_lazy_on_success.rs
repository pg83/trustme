//@ compile-variant: -O
fn main() {
    let mut condition_calls = 0u8;
    let mut message_calls = 0u8;
    assert!({ condition_calls += 1; true }, "{name}: {amount}",
        name = { message_calls += 1; "signal" },
        amount = { message_calls += 1; 463u16 },);
    assert_eq!(condition_calls, 1);
    assert_eq!(message_calls, 0);
}
