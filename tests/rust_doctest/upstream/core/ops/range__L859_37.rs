// Extracted from library/core/src/ops/range.rs:859
#![allow(unused)]
#![feature(range_bounds_is_empty)]
fn main() {
    use std::ops::RangeBounds;

    assert!(!(..0).is_empty());
    assert!(!(i32::MAX..).is_empty());
    assert!(!RangeBounds::<u8>::is_empty(&(..)));
}
