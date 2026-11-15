//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub fn reversed_sum<const N: usize>(value: [u8; 1 + N]) -> [u8; N + 1]
where
    [(); N + 1]:,
    [(); 1 + N]:,
{
    value
}
