//@ compile-fail: error:
trait Element { type Item; }
struct Wrap<T>(T);
impl<T> Element for Wrap<T> { type Item = T; }
fn require<A: Element<Item = B>, B>(_: A, _: B) {}
fn first() -> impl Copy { 59u32 }
struct Factory;
impl Factory { fn second() -> impl Copy { 61u32 } }
fn main() { require(Wrap(first()), Factory::second()); }
