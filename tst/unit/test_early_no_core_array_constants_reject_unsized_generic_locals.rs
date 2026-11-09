//@ compile-fail: Couldn't find type name 'Sized'
#![feature(no_core, generic_const_exprs)]
#![allow(incomplete_features)]
#![no_core]
#![no_main]

struct Packet<T: ?Sized> {
    bytes: [u8; {
        let _value: T;
        1
    }],
}
