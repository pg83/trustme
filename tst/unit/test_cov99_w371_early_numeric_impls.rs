//@ aux-build: cov99_w371_numeric.rs
//@ crate-type: lib
//@ compile-flags: --emit=metadata

use cov99_w371_numeric::ExternalNumber;

trait LocalNumber {}
impl LocalNumber for i32 {}

const fn count<T: LocalNumber + ExternalNumber + Copy>(_: T) -> usize {
    0
}

pub struct Item;
struct Selector;
trait Select<const N: usize> {
    type Receiver;
}
impl Select<0> for Selector {
    type Receiver = Box<Item>;
}

impl Item {
    pub fn touch(self: <Selector as Select<{ count(1) }>>::Receiver) {}
}
