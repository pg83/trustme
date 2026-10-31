//@ compile-fail: invalid left-hand side of assignment
fn main() { (while false {}) = (); }
