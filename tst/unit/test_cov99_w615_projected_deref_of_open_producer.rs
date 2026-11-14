//@ compile-fail: error:

use std::ops::Deref;

trait Family {
    type Out: ?Sized;
}
impl Family for u8 {
    type Out = str;
}
struct Wrap<T>(T);
impl<T: Family> Deref for Wrap<T> {
    type Target = T::Out;
    fn deref(&self) -> &Self::Target { loop {} }
}
trait Produce<T> {
    fn produce() -> T;
}
struct Factory;
impl<T: Family> Produce<Wrap<T>> for Factory {
    fn produce() -> Wrap<T> { loop {} }
}
fn main() {
    let _: &str = &Factory::produce();
}
