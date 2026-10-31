//@ compile-fail: Invalid cast
fn identity(value: u8) -> u8 { value }
fn main() { let _ = identity as fn() -> u8; }
