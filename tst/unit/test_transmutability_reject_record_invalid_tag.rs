//@ compile-fail: TransmuteFrom
#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
#[repr(C)]
struct Checked<'a> { tag: bool, count: u16, payload: &'a u16 }
#[repr(C)]
struct Bytes<'a> { tag: u8, count: u16, payload: &'a u16 }
fn require<D: TransmuteFrom<Bytes<'static>, { Assume::SAFETY }>>() {}
fn main() { require::<Checked<'static>>(); }
