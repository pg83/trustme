// Extracted from library/core/src/ops/range.rs:908
#![allow(unused)]
#![feature(range_into_bounds)]
fn main() {
    use std::ops::Bound::*;
    use std::ops::IntoBounds;

    assert_eq!((0..5).into_bounds(), (Included(0), Excluded(5)));
    assert_eq!((..=7).into_bounds(), (Unbounded, Included(7)));
}
