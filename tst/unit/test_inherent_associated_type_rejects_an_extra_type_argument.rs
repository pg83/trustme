//@ compile-fail: Incorrect generic arguments for inherent associated type
#![feature(inherent_associated_types)]

struct Rack;

impl Rack {
    type Slot<T> = (T,);
}

fn inspect(_value: Rack::Slot<u8, u16>) {}

fn main() {}
