#![feature(transmutability)]

use std::convert::Infallible;
use std::mem::{Assume, TransmuteFrom};

enum One {
    Absent(Infallible),
    Value(u16),
}

enum Two {
    First(u8),
    Absent(Infallible),
    Last(u8),
}

#[repr(C)]
union Word { bits: u16 }

fn require<S, D: TransmuteFrom<S, { Assume::SAFETY }>>() {}

fn main() {
    require::<One, u16>();
    require::<u16, One>();
    require::<Word, u16>();
    require::<u16, Word>();
    require::<Two, [u8; 2]>();

    let value = unsafe { <One as TransmuteFrom<u16, { Assume::SAFETY }>>::transmute(0x1357) };
    match value {
        One::Value(bits) => assert_eq!(bits, 0x1357),
        One::Absent(never) => match never {},
    }
    let word = unsafe { <Word as TransmuteFrom<u16, { Assume::SAFETY }>>::transmute(0x2468) };
    assert_eq!(unsafe { word.bits }, 0x2468);
    for value in [Two::First(7), Two::Last(11)] {
        match value {
            Two::First(bits) => assert_eq!(bits, 7),
            Two::Last(bits) => assert_eq!(bits, 11),
            Two::Absent(never) => match never {},
        }
    }
}
