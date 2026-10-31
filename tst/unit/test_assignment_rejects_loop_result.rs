//@ compile-fail: invalid left-hand side of assignment
fn main() { (loop { break 17u8; }) = 23u8; }
