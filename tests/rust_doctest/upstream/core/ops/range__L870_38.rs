// Extracted from library/core/src/ops/range.rs:870
#![allow(unused)]
#![feature(range_bounds_is_empty)]
fn main() {
    use std::ops::Bound::*;
    use std::ops::RangeBounds;

    assert!(!(Excluded(1), Excluded(3)).is_empty());
    assert!(!(Excluded(1), Excluded(2)).is_empty());
    assert!( (Excluded(1), Excluded(1)).is_empty());
    assert!( (Excluded(2), Excluded(1)).is_empty());
    assert!( (Excluded(3), Excluded(1)).is_empty());
}
