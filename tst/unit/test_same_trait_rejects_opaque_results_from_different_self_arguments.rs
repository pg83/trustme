//@ compile-fail: Failed to find an impl
use std::marker::PhantomData;
trait Same<T> {}
impl<T> Same<T> for T {}
fn require<A: Same<B>, B>(_: A, _: B) {}
struct Factory<T>(PhantomData<T>);
impl<T> Factory<T> { fn value() -> impl Copy { 17u8 } }
fn main() { require(Factory::<u8>::value(), Factory::<u16>::value()); }
