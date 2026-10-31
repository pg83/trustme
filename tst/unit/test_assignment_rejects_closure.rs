//@ compile-fail: invalid left-hand side of assignment
fn main() { (|| 17u8) = || 23u8; }
