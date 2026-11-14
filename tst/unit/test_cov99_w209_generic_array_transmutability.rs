//@ compile-fail: TransmuteFrom
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn require<Source, Destination: TransmuteFrom<Source, { Assume::SAFETY }>>() {}

fn same_array<const N: usize>() {
    require::<[u8; N], [u8; N]>();
}

fn main() {}
