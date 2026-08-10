// Extracted from library/core/src/ops/range.rs:848
#![allow(unused)]
#![feature(range_bounds_is_empty)]
fn main() {
    use std::ops::RangeBounds;

    assert!(!RangeBounds::is_empty(&(3.0..5.0)));
    assert!( RangeBounds::is_empty(&(3.0..f32::NAN)));
    assert!( RangeBounds::is_empty(&(f32::NAN..5.0)));
}
