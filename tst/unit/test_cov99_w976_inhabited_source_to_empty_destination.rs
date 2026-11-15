//@ compile-fail: TransmuteFrom
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

enum Empty {}

fn require<D: TransmuteFrom<(), { Assume::SAFETY }>>() {}

fn main() {
    require::<Empty>();
}
