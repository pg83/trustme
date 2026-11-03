//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
struct Left<T: ?Sized>(*const T);
struct Right<T: ?Sized>(*const T);
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Right<U>> for Left<T> {}
fn main() {}
