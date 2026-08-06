// Extracted from library/core/src/range.rs:135
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::Range;
    
    assert!(!Range::from(3..5).is_empty());
    assert!( Range::from(3..3).is_empty());
    assert!( Range::from(3..2).is_empty());
}
