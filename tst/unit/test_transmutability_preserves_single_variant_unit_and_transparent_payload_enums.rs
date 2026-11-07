#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

enum Unit {
    Only,
}

#[repr(transparent)]
enum Wrapped {
    Only(u32),
}

fn require<Source, Destination>()
where
    Destination: TransmuteFrom<Source, { Assume::SAFETY }>,
{}

fn main() {
    require::<Unit, ()>();
    require::<(), Unit>();
    require::<Wrapped, u32>();
    require::<u32, Wrapped>();

    let unit: Unit = unsafe { std::mem::transmute(()) };
    match unit {
        Unit::Only => assert_eq!(std::mem::size_of::<Unit>(), 0),
    }

    let wrapped: Wrapped = unsafe { std::mem::transmute(0x12345678u32) };
    match wrapped {
        Wrapped::Only(value) => assert_eq!(value, 0x12345678),
    }
    let recovered: u32 = unsafe { std::mem::transmute(Wrapped::Only(0x87654321)) };
    assert_eq!(recovered, 0x87654321);
}
