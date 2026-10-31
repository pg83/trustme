//@ compile-fail: Invalid cast
fn main() { let _ = (|value: u8| value) as fn() -> u8; }
