//@ compile-fail: TransmuteFrom
#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
#[repr(C)]
struct Shared<'a> { tag: bool, count: u16, payload: &'a u16 }
#[repr(C)]
struct Unique<'a> { tag: bool, count: u16, payload: &'a mut u16 }
fn require<D: TransmuteFrom<Shared<'static>, { Assume::SAFETY }>>() {}
fn main() { require::<Unique<'static>>(); }
