//@ compile-fail: TransmuteFrom
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn require_larger_referent<D>()
where
    D: TransmuteFrom<&'static [u8; 1], {
        Assume { alignment: false, lifetimes: false, safety: false, validity: false }
    }>,
{}

fn main() {
    require_larger_referent::<&'static [u8; 2]>();
}
