//@ crate-type: lib
#![feature(coerce_unsized)]

use std::ops::CoerceUnsized;

pub struct Pointer<P>(pub P);

impl<P, Q> CoerceUnsized<Pointer<Q>> for Pointer<P> where P: CoerceUnsized<Q> {}

pub fn freeze<T>(value: Pointer<*mut T>) -> Pointer<*const T>
where
    *mut T: CoerceUnsized<*const T>,
{
    value
}
