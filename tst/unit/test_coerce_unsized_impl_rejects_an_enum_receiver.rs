//@ compile-fail: error
#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::CoerceUnsized;

enum Shell<T: ?Sized> {
    Value(*const T),
}

impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Shell<U>> for Shell<T> {}

fn main() {}
