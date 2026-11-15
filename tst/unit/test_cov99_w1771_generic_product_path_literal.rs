//@ crate-type: lib
//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const WIDTH: usize = 2;

pub fn incompatible_width<const N: usize>(value: [u8; N * 3]) -> [u8; N * WIDTH]
where
    [u8; N * 3]: Sized,
    [u8; N * WIDTH]: Sized,
{
    value
}
