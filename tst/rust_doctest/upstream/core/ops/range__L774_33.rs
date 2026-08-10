// Extracted from library/core/src/ops/range.rs:774
#![allow(unused)]
fn main() {
    use std::ops::Bound::*;
    use std::ops::RangeBounds;

    assert_eq!((..10).start_bound(), Unbounded);
    assert_eq!((3..10).start_bound(), Included(&3));
}
