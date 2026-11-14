//@ compile-fail: TransmuteFrom
#![feature(rustc_attrs, transmutability)]
#![allow(internal_features, dead_code)]

use std::mem::{Assume, TransmuteFrom};

#[rustc_layout_scalar_valid_range_start(1)]
struct Wide([u8; 1usize << 62]);

#[repr(C)]
struct Wrapped(Wide, Wide, Wide, Wide);

fn require<S, D: TransmuteFrom<S, { Assume::SAFETY }>>() {}

fn main() {
    require::<Wrapped, ()>();
}
