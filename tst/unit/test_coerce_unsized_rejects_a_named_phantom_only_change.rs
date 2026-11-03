//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
struct Empty<T: ?Sized> { marker: PhantomData<T>, tag: u32 }
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Empty<U>> for Empty<T> {}
fn main() {}
