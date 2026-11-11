//@ compile-flags: -Zmir-opt-level=3
#![feature(core_intrinsics, adt_const_params)]
#![allow(internal_features, incomplete_features)]

use std::marker::ConstParamTy;

#[derive(Clone, Copy, PartialEq, Eq, ConstParamTy)]
enum Direction {
    Leading(u8),
    Trailing(u8),
}

fn count(direction: Direction, value: u32) -> u32 {
    match direction {
        Direction::Leading(_) => std::intrinsics::ctlz(value),
        Direction::Trailing(_) => std::intrinsics::cttz(value),
    }
}

fn dispatch<const D: Direction>(value: u32) -> u32 {
    count(D, value)
}

fn main() {
    let value = std::hint::black_box(128);
    assert_eq!(dispatch::<{ Direction::Leading(3) }>(value), 24);
    assert_eq!(dispatch::<{ Direction::Trailing(5) }>(value), 7);
}
