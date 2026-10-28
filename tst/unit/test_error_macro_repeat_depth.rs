//@ compile-fail: is still repeating at this depth
macro_rules! bad { ($($x:ident),*) => { fn $x() {} } }
bad!(a, b);
fn main() {}
