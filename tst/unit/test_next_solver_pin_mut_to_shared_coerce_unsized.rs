//@ check-pass
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

fn into_ref<'a, T: ?Sized>(value: PinLike<&'a mut T>) -> PinLike<&'a T> {
    PinLike {
        pointer: value.pointer,
    }
}

fn main() {
    let mut value = 37;
    assert_eq!(*into_ref(PinLike { pointer: &mut value }).pointer, 37);
}
