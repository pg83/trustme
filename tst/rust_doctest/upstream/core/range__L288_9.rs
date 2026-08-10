// Extracted from library/core/src/range.rs:288
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::RangeInclusive;

    assert!(!RangeInclusive::from(3..=5).is_empty());
    assert!(!RangeInclusive::from(3..=3).is_empty());
    assert!( RangeInclusive::from(3..=2).is_empty());
}
