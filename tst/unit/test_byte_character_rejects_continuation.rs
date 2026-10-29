//@ compile-fail: escaped newline is not allowed in a character literal
const BAD: u8 = b'\
    A';
fn main() { let _ = BAD; }
