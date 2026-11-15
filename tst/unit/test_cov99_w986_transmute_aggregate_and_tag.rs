//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(transmutability)]
#![allow(dead_code)]

use core::mem::{Assume, TransmuteFrom};

#[repr(C)]
struct Pair(u8, u8);

#[repr(u8)]
enum Tagged {
    First(u8),
    Second(u8),
}

fn require<Source, Destination>()
where
    Destination: TransmuteFrom<Source, { Assume::SAFETY.and(Assume::VALIDITY) }>,
{
}

pub fn verify_layouts() {
    require::<(u8, u8), Pair>();
    require::<[u8; 2], Tagged>();
}
