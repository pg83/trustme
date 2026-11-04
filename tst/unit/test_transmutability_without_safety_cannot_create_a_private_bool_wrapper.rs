//@ compile-fail: TransmuteFrom
#![feature(transmutability)]
use std::mem::TransmuteFrom;
#[repr(transparent)]
struct Checked(bool);
fn require<D: TransmuteFrom<u8>>() {}
fn main() { require::<Checked>(); }
