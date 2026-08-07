// Extracted from library/core/src/range.rs:321
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::RangeInclusive;

    let mut i = RangeInclusive::from(3..=8).iter().map(|n| n*n);
    assert_eq!(i.next(), Some(9));
    assert_eq!(i.next(), Some(16));
    assert_eq!(i.next(), Some(25));
}
