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

fn require_tag<F>(_: F, _: Tagged<[u8], F>) {}

fn first() {}
fn second() {}

fn main() {
    let array = [1u8, 2];
    require_tag(
        first,
        Tagged {
            data: &array as *const [u8; 2],
            tag: second,
        },
    );
}
