//@ compile-fail: Invalid cast
fn invalid(value: fn(u8) -> u8) -> fn() -> u8 { value as fn() -> u8 }
fn main() {}
