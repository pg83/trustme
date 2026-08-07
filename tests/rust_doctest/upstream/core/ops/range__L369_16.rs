// Extracted from library/core/src/ops/range.rs:369
#![allow(unused)]
fn main() {
    use std::ops::RangeInclusive;

    assert_eq!(3..=5, RangeInclusive::new(3, 5));
}
