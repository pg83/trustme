//@ compile-only
//@ compile-flags: --emit=metadata
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

enum Never {}

enum Sparse {
    Absent(Never),
    Bit(bool),
    Empty,
}

fn require<S, D: TransmuteFrom<S, { Assume::SAFETY }>>() {}

fn main() {
    require::<Sparse, u8>();
}
