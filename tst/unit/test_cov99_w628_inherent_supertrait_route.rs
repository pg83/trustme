//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: multiple applicable items in scope
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

struct Carrier;
impl Carrier {
    type Item = u8;
}

trait Left<T> {
    fn choose(&self);
}
trait Right {
    fn choose(&self);
}
trait Routes: Left<Carrier::Item> + Right {}

fn call(value: &dyn Routes) {
    value.choose();
}
