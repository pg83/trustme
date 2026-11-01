//@ compile-fail: pattern does not mention all fields
struct Record { value: u16 }
fn main() { let Record {} = Record { value: 7 }; }
