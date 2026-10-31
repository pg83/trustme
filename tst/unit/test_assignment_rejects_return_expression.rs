//@ compile-fail: invalid left-hand side of assignment
fn main() { (return) = (); }
