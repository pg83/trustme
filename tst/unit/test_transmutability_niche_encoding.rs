#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

#[repr(u8)]
enum Niche {
    Start = 0,
    End = 253,
}

enum OptionLike {
    Value(Niche),
    FirstEmpty,
    SecondEmpty,
}

fn assert_transmutable<Src, Dst>()
where
    Dst: TransmuteFrom<Src, {
        Assume {
            alignment: false,
            lifetimes: false,
            safety: true,
            validity: false,
        }
    }>,
{
}

fn main() {
    assert_transmutable::<OptionLike, u8>();
}
