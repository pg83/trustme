//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: TransmuteFrom
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn require_reference_transmute<T>(_: T)
where
    for<'a> &'a [T; 0]: TransmuteFrom<&'a [(); 0], { Assume::SAFETY }>,
{
}

fn main() {
    require_reference_transmute(|| ());
}
