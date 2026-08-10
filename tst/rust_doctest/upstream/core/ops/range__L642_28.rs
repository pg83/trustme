// Extracted from library/core/src/ops/range.rs:642
#![allow(unused)]
fn main() {
    use std::ops::Bound::*;
    use std::ops::RangeBounds;

    assert_eq!((..100).start_bound(), Unbounded);
    assert_eq!((1..12).start_bound(), Included(&1));
    assert_eq!((1..12).end_bound(), Excluded(&12));
}
