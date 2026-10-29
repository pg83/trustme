//@ compile-fail: Unknown escape sequence
const BAD: &str = "\q";
fn main() { let _ = BAD; }
