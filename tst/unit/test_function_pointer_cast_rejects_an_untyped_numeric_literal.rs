//@ compile-fail: Invalid cast
fn main() { let _ = 17 as fn() -> u32; }
