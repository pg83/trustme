//@ compile-fail: invalid left-hand side of assignment
fn main() { (match true { true => 17u8, false => 23 }) = 31u8; }
