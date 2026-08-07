// Extracted from library/core/src/ops/range.rs:922
#![allow(unused)]
#![feature(range_into_bounds)]
fn main() {
    use std::ops::Bound::*;
    use std::ops::IntoBounds;

    assert_eq!((3..).intersect(..5), (Included(3), Excluded(5)));
    assert_eq!((-12..387).intersect(0..256), (Included(0), Excluded(256)));
    assert_eq!((1..5).intersect(..), (Included(1), Excluded(5)));
    assert_eq!((1..=9).intersect(0..10), (Included(1), Included(9)));
    assert_eq!((7..=13).intersect(8..13), (Included(8), Excluded(13)));
}
