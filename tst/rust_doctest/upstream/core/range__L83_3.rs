// Extracted from library/core/src/range.rs:83
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::Range;

    let mut i = Range::from(3..9).iter().map(|n| n*n);
    assert_eq!(i.next(), Some(9));
    assert_eq!(i.next(), Some(16));
    assert_eq!(i.next(), Some(25));
}
