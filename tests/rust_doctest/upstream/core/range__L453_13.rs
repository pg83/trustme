// Extracted from library/core/src/range.rs:453
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::RangeFrom;
    
    let mut i = RangeFrom::from(3..).iter().map(|n| n*n);
    assert_eq!(i.next(), Some(9));
    assert_eq!(i.next(), Some(16));
    assert_eq!(i.next(), Some(25));
}
