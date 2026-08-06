// Extracted from library/core/src/range.rs:222
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::RangeInclusive;
    
    assert_eq!(RangeInclusive::from(3..=5), RangeInclusive { start: 3, end: 5 });
    assert_eq!(3 + 4 + 5, RangeInclusive::from(3..=5).into_iter().sum());
}
