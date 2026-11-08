//@ compile-fail: macro_rules! requires an identifier
macro_rules! { () => {} }
fn main() {}
