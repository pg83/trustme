//@ compile-fail: error
macro_rules! only { ($definition:item) => { $definition }; }
only!(type Broken where (): ? = u8;);
fn main() {}
