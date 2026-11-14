//@ compile-fail: Failed to find an impl of
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn require<S, D>(_: S)
where
    D: TransmuteFrom<S, { Assume::SAFETY }>,
{
}

fn main() {
    let byte = 2_u8;
    let capture = move || byte;
    require::<_, bool>((capture,));
}
