#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::CoerceUnsized;

struct Wrapper<'a, T: ?Sized + 'a>(&'a T);

impl<'a, T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Wrapper<'a, U>> for Wrapper<'a, T> {}

fn coerce<'a, T, U: ?Sized>(value: Wrapper<'a, T>) -> Wrapper<'a, U>
where
    Wrapper<'a, T>: CoerceUnsized<Wrapper<'a, U>>,
{
    value
}

fn double(value: i32) -> i32 {
    value * 2
}

fn main() {
    let array = [1, 2, 3];
    let slice = coerce::<_, [i32]>(Wrapper(&array));
    assert_eq!(slice.0, &[1, 2, 3]);

    let pointer: fn(i32) -> i32 = double;
    let callable: Wrapper<'_, dyn Fn(i32) -> i32> = coerce(Wrapper(&pointer));
    assert_eq!((callable.0)(21), 42);
}
