//@ compile-fail: Failed to find an impl
trait Same<T> {}
impl<T> Same<T> for T {}
fn require<A: Same<B>, B>(_: A, _: B) {}
trait Factory { fn value() -> impl Copy; }
impl Factory for u8 { fn value() -> impl Copy { 17u8 } }
impl Factory for u16 { fn value() -> impl Copy { 23u8 } }
fn main() { require(<u8 as Factory>::value(), <u16 as Factory>::value()); }
