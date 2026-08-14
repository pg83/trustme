#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

#[repr(C)]
struct Unit;

fn assert_transmutable<Src, Dst>()
where
    Dst: TransmuteFrom<Src, {
        Assume {
            alignment: true,
            lifetimes: false,
            safety: true,
            validity: false,
        }
    }>,
{
}

fn main() {
    assert_transmutable::<&'static Unit, &'static Unit>();
}
