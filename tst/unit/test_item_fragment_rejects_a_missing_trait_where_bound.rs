//@ compile-fail: error
macro_rules! only { ($definition:item) => { $definition }; }
only!(trait Broken where (): ? {});
fn main() {}
