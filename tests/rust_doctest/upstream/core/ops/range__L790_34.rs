// Extracted from library/core/src/ops/range.rs:790
#![allow(unused)]
fn main() {
    use std::ops::Bound::*;
    use std::ops::RangeBounds;
    
    assert_eq!((3..).end_bound(), Unbounded);
    assert_eq!((3..10).end_bound(), Excluded(&10));
}
