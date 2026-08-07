// Extracted from library/core/src/ops/range.rs:936
#![allow(unused)]
#![feature(range_into_bounds)]
#![feature(range_bounds_is_empty)]
fn main() {
    use std::ops::{RangeBounds, IntoBounds};

    assert!(!(3..).intersect(..5).is_empty());
    assert!(!(-12..387).intersect(0..256).is_empty());
    assert!((1..5).intersect(6..).is_empty());
}
