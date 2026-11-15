#![feature(pattern_types)]

use std::pat::pattern_type;

struct Payload(pattern_type!(u8 is 9..=14 | 4..=10));

fn main() {
    assert_eq!(std::mem::size_of::<Payload>(), 1);
}
