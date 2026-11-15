//@ crate-type: lib
//@ compile-flags: -Z mir-opt-level=0 -Z dump-mir
#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

pub struct Tail<T: ?Sized>(pub T);

fn first(value: Tail<[u8]>) -> u8 {
    match value.0 {
        [byte, ..] => byte,
        _ => 0,
    }
}

pub fn forward(value: Tail<[u8]>) -> u8 {
    first(value)
}
