// Extracted from library/core/src/range.rs:421
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::RangeFrom;
    
    assert_eq!(RangeFrom::from(2..), core::range::RangeFrom { start: 2 });
    assert_eq!(2 + 3 + 4, RangeFrom::from(2..).into_iter().take(3).sum());
}
