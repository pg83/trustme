//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(sized_hierarchy)]

use core::marker::MetaSized;

pub struct Slot<T: MetaSized>(*const T);

pub trait Project {
    type Output;
}

impl<T: MetaSized> Project for *const T {
    type Output = Slot<T>;
}

impl<T: MetaSized> Slot<T> {
    pub fn identity(value: &T) -> &T {
        value
    }
}

pub fn projected_lookup(value: &u8) -> &u8 {
    <<*const _ as Project>::Output>::identity(value)
}
