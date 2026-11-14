//@ compile-fail: TransmuteFrom
#![feature(extern_types, transmutability, trivial_bounds)]

use std::mem::{Assume, TransmuteFrom};

extern "C" {
    type Foreign;
}

fn require<S, D: TransmuteFrom<S, { Assume::SAFETY }>>() {}

fn reject_foreign_source()
where
    Foreign: Sized,
{
    require::<Foreign, u8>();
}

fn main() {}
