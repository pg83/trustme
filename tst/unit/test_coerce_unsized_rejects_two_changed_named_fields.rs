//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
struct Pair<T: ?Sized> { first: *const T, second: *const T }
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Pair<U>> for Pair<T> {}
fn main() {}
