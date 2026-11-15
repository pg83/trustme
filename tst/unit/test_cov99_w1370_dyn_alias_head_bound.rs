//@ compile-fail: Select
#![feature(trait_alias)]

trait Required {}
trait Other {
    type Item;
}
trait Carrier {
    type Item;
}
trait Bounded = Carrier<Item: Required>;
trait Select<T> {}
struct Adapter<T: ?Sized>(*const T);
impl<T: Carrier + ?Sized> Select<T::Item> for Adapter<T> {}

fn need<T: Select<u8>>() {}
fn probe<U: Other>() {
    need::<Adapter<dyn Bounded<Item = U::Item>>>();
}
fn main() {}
