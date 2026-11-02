//@ compile-fail: Failed to find an impl
trait Same<T> {}
impl<T> Same<T> for T {}
fn require<A: Same<B>, B>(_: A, _: B) {}
fn values() -> (impl Copy, impl Copy) { (17u8, 23u8) }
fn main() { let (first, second) = values(); require(first, second); }
