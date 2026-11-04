//@ compile-fail: TransmuteFrom
#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
#[repr(C)]
union Wide { whole: u16, narrow: u8 }
fn require<D: TransmuteFrom<Wide, { Assume::SAFETY }>>() {}
fn main() { require::<[u8; 2]>(); }
