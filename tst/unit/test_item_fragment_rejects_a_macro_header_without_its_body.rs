//@ compile-fail: error
#![feature(decl_macro)]
macro_rules! only { ($definition:item) => { $definition }; }
only!(macro incomplete($value:expr));
fn main() {}
