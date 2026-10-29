//@ compile-fail: Failed to find an impl
#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
fn require<D: TransmuteFrom<u32, { Assume { alignment: false, lifetimes: false, safety: true, validity: false } }>>() {}
fn main() { require::<char>(); }
