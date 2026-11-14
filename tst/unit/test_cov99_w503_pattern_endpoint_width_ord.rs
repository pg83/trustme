//@ crate-type: lib
//@ compile-fail: Invalid cast [dst]
#![feature(pattern_types, pattern_type_macro)]

use std::pat::pattern_type;

type ByteRange = pattern_type!(u8 is 0..=1);
type WordRange = pattern_type!(u16 is 0..=1);

pub fn cast_range(value: ByteRange) -> WordRange {
    value as WordRange
}
