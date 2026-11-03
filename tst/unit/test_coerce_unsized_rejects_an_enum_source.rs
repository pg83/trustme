//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
enum Source { Pointer(*const u8) }
struct Target(*const u8);
impl CoerceUnsized<Target> for Source {}
fn main() {}
