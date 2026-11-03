//@ compile-fail: error:
trait Element { type Item; }
struct Wrap<T>(T);
impl<T> Element for Wrap<T> { type Item = T; }
fn require<A: Element<Item = B>, B>(_: A, _: B) {}
struct Factory;
impl Factory {
    fn first() -> impl Copy { 41u32 }
    fn second() -> impl Copy { 43u32 }
}
fn main() { require(Wrap(Factory::first()), Factory::second()); }
