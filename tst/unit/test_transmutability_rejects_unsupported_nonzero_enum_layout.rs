//@ compile-fail: TransmuteFrom
#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
use std::num::NonZeroU8;
enum Nullable { Empty, Value(NonZeroU8) }
fn require<D: TransmuteFrom<Nullable, { Assume::SAFETY }>>() {}
fn main() { require::<u8>(); }
