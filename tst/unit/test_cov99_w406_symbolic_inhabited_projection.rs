//@ crate-type: lib
//@ env: TRUSTME_CAPS_ORACLE=1
#![feature(core_intrinsics, generic_const_exprs)]
#![allow(incomplete_features)]

pub trait Produce {
    type Item;
}

const fn inhabited_length<T: Produce>() -> usize {
    core::intrinsics::assert_inhabited::<T::Item>();
    1
}

pub fn projected_array<T: Produce>() -> [u8; inhabited_length::<T>()] {
    [7; inhabited_length::<T>()]
}
