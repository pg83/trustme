//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
struct Source<T>(*const T);
impl CoerceUnsized<fn()> for Source<u16> {}
fn main() {}
