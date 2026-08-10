// Extracted from library/core/src/ops/range.rs:835
#![allow(unused)]
#![feature(range_bounds_is_empty)]
fn main() {
    use std::ops::RangeBounds;

    assert!(!(3..).is_empty());
    assert!(!(..2).is_empty());
    assert!(!RangeBounds::is_empty(&(3..5)));
    assert!( RangeBounds::is_empty(&(3..3)));
    assert!( RangeBounds::is_empty(&(3..2)));
}
