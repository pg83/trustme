//@ compile-fail: invalid left-hand side of assignment
fn main() { let mut value = 0; (value = 1) = (); }
