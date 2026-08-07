// Extracted from library/core/src/range.rs:47
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::Range;

    assert_eq!(Range::from(3..5), Range { start: 3, end: 5 });
    assert_eq!(3 + 4 + 5, Range::from(3..6).into_iter().sum());
}
