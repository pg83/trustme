//@ compile-fail: Unknown fragment type
macro_rules! bad { ($x:unknown) => {} }
fn main() {}
