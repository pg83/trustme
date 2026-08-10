// Extracted from library/core/src/range.rs:299
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::RangeInclusive;

    assert!(!RangeInclusive::from(3.0..=5.0).is_empty());
    assert!( RangeInclusive::from(3.0..=f32::NAN).is_empty());
    assert!( RangeInclusive::from(f32::NAN..=5.0).is_empty());
}
