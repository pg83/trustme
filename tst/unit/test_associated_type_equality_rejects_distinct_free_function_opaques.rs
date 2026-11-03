//@ compile-fail: error:
trait Element { type Item; }
struct Wrap<T>(T);
impl<T> Element for Wrap<T> { type Item = T; }
fn require<A: Element<Item = B>, B>(_: A, _: B) {}
fn first() -> impl Copy { 31u32 }
fn second() -> impl Copy { 37u32 }
fn main() { require(Wrap(first()), second()); }
