//@ compile-fail: One CoercePointee type parameter must have
#![feature(derive_coerce_pointee)]
use std::marker::{CoercePointee, PhantomData};
#[derive(CoercePointee)]
#[repr(transparent)]
struct Pointer<T: ?Sized, U: ?Sized>(*const T, PhantomData<U>);
fn main() {}
