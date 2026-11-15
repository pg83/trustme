//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub fn nested_sum<const N: usize>(value: [u8; (N + 1) + 1]) -> [u8; N + 1]
where
    [(); N + 1]:,
    [(); (N + 1) + 1]:,
{
    value
}
