//@ compile-fail: different source
trait Same<T> {}
impl<T> Same<T> for T {}
fn require<A: Same<B>, B>(_: A, _: B) {}
struct Factory;
impl Factory {
    fn first() -> impl Copy { 17u8 }
    fn second() -> impl Copy { 23u8 }
}
fn main() { require(Factory::first(), Factory::second()); }
