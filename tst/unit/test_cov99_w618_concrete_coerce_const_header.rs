//@ compile-fail: Type mismatch
//@ compile-flags: -Cpanic=abort
#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::CoerceUnsized;

struct View<T: ?Sized, const N: usize>(*const T, [u8; N]);

impl<T: ?Sized + Unsize<U>, U: ?Sized, const N: usize> CoerceUnsized<View<U, N>>
    for View<T, N>
{
}

fn main() {
    let values = [5u8, 6, 7];
    let source = View::<[u8; 3], 1>(&values, [0]);
    let _: View<[u8], 2> = source;
}
