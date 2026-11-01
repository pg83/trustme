//@ compile-fail: cycle
const FIRST: u8 = match 13u8 { SECOND => 3, _ => 7 };
const SECOND: u8 = match 17u8 { FIRST => 5, _ => 11 };
fn main() { std::hint::black_box(FIRST); }
