//@ compile-fail: error
macro_rules! only { ($definition:item) => { $definition }; }
only!(trait Broken: for<'a Missing {});
fn main() {}
