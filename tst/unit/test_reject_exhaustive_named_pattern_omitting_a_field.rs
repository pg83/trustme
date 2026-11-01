//@ compile-fail: pattern does not mention all fields
struct Record { first: u8, second: u16 }
fn main() { let Record { first } = Record { first: 11, second: 13 }; let _ = first; }
