//@ compile-fail: TransmuteFrom
#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
#[repr(C)]
union Choice { checked: bool, raw: u8 }
fn require<D: TransmuteFrom<Choice, { Assume::SAFETY }>>() {}
fn main() { require::<bool>(); }
