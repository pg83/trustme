//@ compile-fail: TransmuteFrom
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

#[repr(align(1))]
union Empty {}

fn require<D: TransmuteFrom<u8, { Assume::SAFETY }>>() {}

fn main() {
    require::<Empty>();
}
