//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
struct Source(*const u16);
union Target { pointer: *const u16 }
impl CoerceUnsized<Target> for Source {}
fn main() {}
