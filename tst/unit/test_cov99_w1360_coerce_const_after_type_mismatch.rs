//@ compile-fail: Type mismatch
#![feature(coerce_unsized, unsize)]

use std::marker::{PhantomData, Unsize};
use std::ops::CoerceUnsized;

struct Tagged<T: ?Sized, U, const N: usize> {
    ptr: *const T,
    marker: PhantomData<U>,
}

impl<T: ?Sized + Unsize<V>, V: ?Sized, U, const N: usize>
    CoerceUnsized<Tagged<V, U, N>> for Tagged<T, U, N>
{
}

fn main() {
    let bytes = [11u8, 22, 33];
    let source = Tagged::<[u8; 3], u8, 7> {
        ptr: &bytes,
        marker: PhantomData,
    };
    let _: Tagged<[u8], u16, _> = source;
}
