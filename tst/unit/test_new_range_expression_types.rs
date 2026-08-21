//@ crate-type: lib
#![feature(new_range_api)]
#![feature(new_range)]

pub fn from() -> core::range::RangeFrom<u8> {
    1..
}

pub fn half_open() -> core::range::Range<u8> {
    1..2
}

pub fn inclusive() -> core::range::RangeInclusive<u8> {
    1..=2
}
