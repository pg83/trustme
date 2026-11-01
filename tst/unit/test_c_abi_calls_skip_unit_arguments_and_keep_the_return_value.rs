//@ compile-variant: -O
#![allow(improper_ctypes_definitions)]

extern "C" fn add(_: (), value: u16) -> u16 { value + 3 }
extern "C" fn only_unit(_: ()) -> u16 { 17 }
extern "C" fn around(_: (), value: u16, _: ()) -> u16 { value + 5 }
#[inline(never)]
fn indirect(call: extern "C" fn((), u16) -> u16, value: u16) -> u16 {
    call((), value) + 7
}
#[inline(never)]
fn indirect_unit(call: extern "C" fn(()) -> u16) -> u16 { call(()) + 11 }
#[inline(never)]
fn indirect_around(call: extern "C" fn((), u16, ()) -> u16, value: u16) -> u16 {
    call((), value, ()) + 13
}

fn main() {
    assert_eq!(add((), 11), 14);
    assert_eq!(indirect(add, 13), 23);
    assert_eq!(indirect_unit(only_unit), 28);
    assert_eq!(indirect_around(around, 17), 35);
}
