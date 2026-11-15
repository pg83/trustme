#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::CoerceUnsized;

struct Tagged<T: ?Sized, const N: usize> {
    ptr: *const T,
}

impl<T: ?Sized + Unsize<U>, U: ?Sized, const N: usize> CoerceUnsized<Tagged<U, N>>
    for Tagged<T, N>
{
}

fn tag<T: ?Sized, const N: usize>(_: &Tagged<T, N>) -> usize {
    N
}

fn main() {
    let bytes = [11u8, 22, 33];
    let source = Tagged::<[u8; 3], 7> { ptr: &bytes };
    let narrowed: Tagged<[u8], _> = source;
    assert_eq!(tag(&narrowed), 7);
    assert_eq!(unsafe { &*narrowed.ptr }, &bytes[..]);
}
