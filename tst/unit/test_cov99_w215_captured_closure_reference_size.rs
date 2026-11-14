//@ compile-fail: TransmuteFrom
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn require_mutable<'a, T>(_: &'a T)
where
    &'a mut T: TransmuteFrom<&'a T, { Assume::SAFETY }>,
{}

fn main() {
    let byte = 37u8;
    let captured = move || byte;
    require_mutable(&captured);
}
