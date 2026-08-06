// Extracted from library/core/src/ops/range.rs:745
#![allow(unused)]
fn main() {
    use std::ops::Bound::*;
    use std::ops::RangeBounds;
    
    assert_eq!((1..12).start_bound(), Included(&1));
    assert_eq!((1..12).start_bound().cloned(), Included(1));
}
