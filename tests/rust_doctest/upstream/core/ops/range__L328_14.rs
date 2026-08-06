// Extracted from library/core/src/ops/range.rs:328
#![allow(unused)]
fn main() {
    assert_eq!((3..=5), std::ops::RangeInclusive::new(3, 5));
    assert_eq!(3 + 4 + 5, (3..=5).sum());
}
