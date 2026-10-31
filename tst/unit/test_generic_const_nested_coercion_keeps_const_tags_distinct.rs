//@ compile-fail: Type mismatch
#![feature(coerce_unsized, generic_const_exprs)]
#![allow(incomplete_features)]

use std::ops::CoerceUnsized;

const fn count(n: usize) -> usize { n + 1 }

struct Handle<P, const N: usize> { pointer: P, tag: [u8; N] }
impl<P, Q, const N: usize> CoerceUnsized<Handle<Q, N>> for Handle<P, N>
where P: CoerceUnsized<Q> {}

struct Outer<T>(T);
impl<T: CoerceUnsized<U>, U> CoerceUnsized<Outer<U>> for Outer<T> {}

fn widen<const N: usize>(value: Outer<Handle<*const [u8; 4], { count(N) }>>)
    -> Outer<Handle<*const [u8], { count(N + 1) }>>
where [(); count(N)]:, [(); count(N + 1)]: { value }

fn main() {}
