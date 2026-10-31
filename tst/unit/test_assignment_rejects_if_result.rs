//@ compile-fail: invalid left-hand side of assignment
fn main() { (if true { 17u8 } else { 23u8 }) = 31u8; }
