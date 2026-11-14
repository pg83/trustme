//@ crate-type: lib
//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const STEP: usize = 2;

pub fn unequal<const N: usize>(value: [u8; N + (1 + 0)]) -> [u8; N + STEP]
where
    [(); N + (1 + 0)]: Sized,
    [(); N + STEP]: Sized,
{
    value
}
