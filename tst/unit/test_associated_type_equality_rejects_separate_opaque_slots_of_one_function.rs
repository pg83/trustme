//@ compile-fail: error:
trait Element { type Item; }
struct Wrap<T>(T);
impl<T> Element for Wrap<T> { type Item = T; }
fn require<A: Element<Item = B>, B>(_: A, _: B) {}
fn pair() -> (impl Copy, impl Copy) { (67u32, 71u32) }
fn main() { let (left, right) = pair(); require(Wrap(left), right); }
