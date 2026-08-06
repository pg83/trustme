// Extracted from library/core/src/ops/range.rs:560
#![allow(unused)]
fn main() {
    assert_eq!((..=5), std::ops::RangeToInclusive{ end: 5 });
}
