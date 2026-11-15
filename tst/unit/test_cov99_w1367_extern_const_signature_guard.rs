//@ edition: 2021
//@ compile-fail: generic arguments are not allowed on extern type
#![feature(extern_types)]

extern "C" {
    type Opaque;
}

pub fn identity<const N: usize>(pointer: *const Opaque<N>) -> *const Opaque<N> {
    pointer
}

fn main() {}
