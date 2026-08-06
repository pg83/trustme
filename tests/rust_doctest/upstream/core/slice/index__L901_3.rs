// Extracted from library/core/src/slice/index.rs:901
#![allow(unused)]
#![feature(slice_range)]
fn main() {
    
    use std::slice;
    
    assert_eq!(None, slice::try_range(2..1, ..3));
    assert_eq!(None, slice::try_range(1..4, ..3));
    assert_eq!(None, slice::try_range(1..=usize::MAX, ..3));
}
