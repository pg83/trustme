//@ compile-fail: Invalid cast
fn main() { let _ = true as fn() -> u8; }
