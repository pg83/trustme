//@ compile-fail: TransmuteFrom
//@ compile-flags: --emit=metadata
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn require_valid_referent<D>()
where
    D: TransmuteFrom<&'static u8, {
        Assume {
            alignment: false,
            lifetimes: false,
            safety: true,
            validity: false,
        }
    }>,
{}

fn main() {
    require_valid_referent::<&'static bool>();
}
