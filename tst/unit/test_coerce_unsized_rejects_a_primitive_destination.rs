//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
struct Source(*const u8);
impl CoerceUnsized<usize> for Source {}
fn main() {}
