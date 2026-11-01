//@ edition: 2021
//@ compile-fail: expected
macro_rules! bad { ($($name:ident),*) => { $($name),! }; }
fn main() { bad!(known); }
