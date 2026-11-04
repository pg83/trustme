//@ compile-variant: -O
use std::any::TypeId;

struct Carrier<T>(T);
union Choice<T: Copy> { callback: T, empty: () }
enum Envelope<T> { Callback(T) }

fn add_three(value: &u8) -> u8 { *value + 3 }
fn add_five(value: &u8) -> u8 { *value + 5 }

#[inline(never)]
fn carrier(first: bool) -> Carrier<fn(&'static u8) -> u8> {
    if first {
        Carrier::<for<'a> fn(&'a u8) -> u8>(add_three)
    } else {
        Carrier::<fn(&'static u8) -> u8>(add_five)
    }
}

#[inline(never)]
fn choice(first: bool) -> Choice<fn(&'static u8) -> u8> {
    if first {
        Choice::<for<'a> fn(&'a u8) -> u8> { callback: add_three }
    } else {
        Choice::<fn(&'static u8) -> u8> { callback: add_five }
    }
}

#[inline(never)]
fn envelope(first: bool) -> Envelope<fn(&'static u8) -> u8> {
    if first {
        Envelope::<for<'a> fn(&'a u8) -> u8>::Callback(add_three)
    } else {
        Envelope::<fn(&'static u8) -> u8>::Callback(add_five)
    }
}

fn main() {
    static VALUE: u8 = 13;
    assert_ne!(TypeId::of::<Carrier<for<'a> fn(&'a u8) -> u8>>(), TypeId::of::<Carrier<fn(&'static u8) -> u8>>());
    for first in [false, true] {
        let expected = if first { 16 } else { 18 };
        assert_eq!((carrier(first).0)(&VALUE), expected);
        assert_eq!(unsafe { (choice(first).callback)(&VALUE) }, expected);
        let Envelope::Callback(callback) = envelope(first);
        assert_eq!(callback(&VALUE), expected);
    }
}
