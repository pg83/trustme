//@ compile-fail: Type mismatch
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::CoerceUnsized;

struct Tagged<T: ?Sized, F> {
    data: *const T,
    tag: F,
}

impl<T: ?Sized + Unsize<U>, U: ?Sized, F> CoerceUnsized<Tagged<U, F>> for Tagged<T, F> {}

fn expected_tag() -> impl Copy {
    0u8
}

fn source() -> Tagged<[u8; 2], impl Copy> {
    Tagged {
        data: std::ptr::null(),
        tag: 0u16,
    }
}

fn require_tag<F>(_: F, _: Tagged<[u8], F>) {}

fn main() {
    require_tag(expected_tag(), source());
}
