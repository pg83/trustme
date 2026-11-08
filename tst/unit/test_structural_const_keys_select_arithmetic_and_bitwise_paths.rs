#![feature(adt_const_params)]
#![allow(incomplete_features)]

use std::marker::ConstParamTy;

#[derive(PartialEq, Eq, ConstParamTy)]
struct Key(u8, bool);

fn apply<const KEY: Key>(value: u8) -> u8 {
    if KEY.1 {
        value ^ KEY.0
    } else {
        value + KEY.0
    }
}

fn main() {
    assert_eq!(apply::<{ Key(3, false) }>(7), 10);
    assert_eq!(apply::<{ Key(5, true) }>(12), 9);
}
