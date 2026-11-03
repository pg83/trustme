//@ compile-fail: Unexpected token
macro_rules! invalid { ($crate:tt) => { () }; }
fn main() {}
