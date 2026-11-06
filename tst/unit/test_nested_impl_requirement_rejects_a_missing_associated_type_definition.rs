//@ compile-fail: Failed to find an impl
trait Source { type Value; }
impl Source for u8 {}
trait Accepted {}
impl<T: Source<Value = u16>> Accepted for T {}
fn require<T: Accepted>() {}
fn main() { require::<u8>(); }
