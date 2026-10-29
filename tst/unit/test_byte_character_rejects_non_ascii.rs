//@ compile-fail: non-ASCII character in byte literal
const BAD: u8 = b'λ';
fn main() { let _ = BAD; }
