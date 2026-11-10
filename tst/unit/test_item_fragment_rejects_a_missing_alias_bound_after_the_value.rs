//@ compile-fail: error
macro_rules! only { ($definition:item) => { $definition }; }
only!(type Broken = u8 where (): ?;);
fn main() {}
