#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn require<S, D>(_: S)
where
    D: TransmuteFrom<S, { Assume::SAFETY }>,
{}

fn main() {
    require::<_, u8>((5,));
}
