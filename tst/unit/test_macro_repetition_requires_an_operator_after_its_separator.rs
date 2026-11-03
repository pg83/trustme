//@ compile-fail: Unexpected token
macro_rules! invalid { ($($item:tt),) => { () }; }
fn main() {}
