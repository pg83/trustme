//@ compile-fail: Unexpected token
macro_rules! invalid { ($1:tt) => { () }; }
fn main() {}
