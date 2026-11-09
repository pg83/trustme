//@ compile-fail: Incorrect generic arguments for inherent associated type
#![feature(coerce_unsized, inherent_associated_types)]

use std::ops::CoerceUnsized;

struct Rack;

impl Rack {
    type Slot<T> = *const T;
}

struct Wrap<T>(Rack::Slot<T, u16>);

impl CoerceUnsized<Wrap<u16>> for Wrap<u8> {}

fn main() {}
