//@ compile-fail: different source
trait Same<T> {}
impl<T> Same<T> for T {}
fn require<A: Same<B>, B>(_: A, _: B) {}
fn value() -> impl Copy { 17u8 }
struct Factory;
impl Factory { fn value() -> impl Copy { 23u8 } }
fn main() { require(value(), Factory::value()); }
