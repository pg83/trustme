//@ compile-fail: Cannot find an impl of
//@ compile-flags: -Znext-solver

#![feature(coerce_unsized)]

use std::ops::CoerceUnsized;

struct PinLike<Pointer> {
    pointer: Pointer,
}

impl<Pointer, Target> CoerceUnsized<PinLike<Target>> for PinLike<Pointer>
where
    Pointer: CoerceUnsized<Target>,
{
}

fn invalid_outer_coercion<'a, T: ?Sized>(
    value: PinLike<&'a mut T>,
) -> PinLike<&'a T> {
    value
}

fn main() {}
