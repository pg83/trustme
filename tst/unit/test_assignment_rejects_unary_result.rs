//@ compile-fail: invalid left-hand side of assignment
fn main() { (!true) = false; }
