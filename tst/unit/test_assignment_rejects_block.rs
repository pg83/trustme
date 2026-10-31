//@ compile-fail: invalid left-hand side of assignment
fn main() { ({ 1u8 }) = 2u8; }
