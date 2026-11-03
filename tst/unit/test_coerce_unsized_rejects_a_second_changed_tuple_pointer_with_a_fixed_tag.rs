//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
struct Both<T: ?Sized>(u16, *const T, *mut T);
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Both<U>> for Both<T> {}
fn main() {}
