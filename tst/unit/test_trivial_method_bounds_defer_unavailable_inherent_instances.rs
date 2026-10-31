//@ crate-type: lib
#![feature(trivial_bounds)]
pub struct Missing;
struct Holder;
impl Holder {
    fn required<T: Clone>(value: &T) -> T { value.clone() }
}
pub fn deferred<U>(value: &Missing) -> Missing where Missing: Clone {
    Holder::required(value)
}
