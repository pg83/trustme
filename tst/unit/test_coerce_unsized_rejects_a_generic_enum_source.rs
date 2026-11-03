//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
enum Source<T> { Value(*const T), Empty }
struct Target<T>(*const T);
impl CoerceUnsized<Target<u32>> for Source<u32> {}
fn main() {}
