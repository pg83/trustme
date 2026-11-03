//@ compile-fail: error:
trait Element { type Item; }
struct Wrap<T>(T);
impl<T> Element for Wrap<T> { type Item = T; }
fn require<A: Element<Item = B>, B>(_: A, _: B) {}
trait Left { fn make() -> impl Copy; }
trait Right { fn make() -> impl Copy; }
struct Factory;
impl Left for Factory { fn make() -> impl Copy { 47u32 } }
impl Right for Factory { fn make() -> impl Copy { 53u32 } }
fn compare<T: Left + Right>() { require(Wrap(<T as Left>::make()), <T as Right>::make()); }
fn main() { compare::<Factory>(); }
