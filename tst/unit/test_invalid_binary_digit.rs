//@ compile-fail: Invalid digit in binary literal
const BAD: u8 = 0b102;
fn main() { let _ = BAD; }
