//@ compile-fail: TransmuteFrom
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn check<D>()
where
    D: TransmuteFrom<&'static u8, { Assume { alignment: false, lifetimes: false, safety: true, validity: false } }>,
{}

fn main() { check::<&'static mut u8>(); }
