#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

#[repr(C)]
struct A(&'static B);

#[repr(C)]
struct B(&'static A);

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
    assert_transmutable::<&'static A, &'static B>();
    assert_transmutable::<&'static B, &'static A>();
}
