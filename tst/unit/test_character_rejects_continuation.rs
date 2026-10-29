//@ compile-fail: escaped newline is not allowed in a character literal
const BAD: char = '\
    A';
fn main() { let _ = BAD; }
