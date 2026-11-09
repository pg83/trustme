//@ compile-fail: Cannot implement CoerceUnsized on non-structs
#![feature(coerce_unsized, unsize)]

enum Handle<T: ?Sized> {
    Pointer(*const T),
}

impl<T: ?Sized + std::marker::Unsize<U>, U: ?Sized>
    std::ops::CoerceUnsized<Handle<U>> for Handle<T>
{
}

fn main() {}
