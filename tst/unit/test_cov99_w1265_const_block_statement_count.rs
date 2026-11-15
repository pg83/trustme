//@ compile-fail: mismatch between
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn unequal_blocks<const N: usize>(
    source: [u8; { N; N + 1 }],
) -> [u8; { N; N; N + 2 }] {
    source
}

fn main() {}
