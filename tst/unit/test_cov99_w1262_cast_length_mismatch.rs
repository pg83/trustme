//@ crate-type: lib
//@ compile-flags: -Cpanic=abort
//@ compile-fail: mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub fn different_lengths<const N: usize>(value: [u8; N + 1]) -> [u8; N as usize]
where
    [(); N + 1]: Sized,
    [(); N as usize]: Sized,
{
    value
}
