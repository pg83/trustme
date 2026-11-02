//@ compile-variant: -O
use std::marker::PhantomData;
trait Same<T> {}
impl<T> Same<T> for T {}
fn require<A: Same<B>, B>(_: A, _: B) {}
fn value() -> impl Copy { 17u8 }
struct Factory<T>(PhantomData<T>);
impl<T> Factory<T> { fn value() -> impl Copy { 23u8 } }
trait Produce { fn value() -> impl Copy; }
impl Produce for u16 { fn value() -> impl Copy { 29u8 } }
fn main() {
    require(value(), value());
    require(Factory::<u16>::value(), Factory::<u16>::value());
    require(<u16 as Produce>::value(), <u16 as Produce>::value());
}
