#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::CoerceUnsized;

struct Tail<T: ?Sized> {
    stamp: u16,
    payload: T,
}

struct Address<T: ?Sized> {
    pointer: *const Tail<T>,
    cookie: u8,
}

impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Address<U>> for Address<T> {}

fn main() {
    let value = Tail { stamp: 37, payload: [17u16, 19, 23] };
    let address: Address<[u16]> = Address { pointer: &value, cookie: 41 };
    let view = unsafe { &*address.pointer };
    assert_eq!(address.cookie, 41);
    assert_eq!(view.stamp, 37);
    assert_eq!(&view.payload, &[17, 19, 23]);
    assert_eq!(view.payload.as_ptr(), value.payload.as_ptr());
}
