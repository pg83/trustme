//@ crate-type: lib
//@ compile-flags: -Cpanic=abort

pub trait Tail {}

pub fn tail(value: &(u8, dyn Tail)) -> &dyn Tail {
    &value.1
}
