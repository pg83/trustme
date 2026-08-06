// Extracted from library/core/src/slice/mod.rs:4861
#![allow(unused)]
#![feature(substr_range)]
fn main() {
    
    let nums = &[0, 5, 10, 0, 0, 5];
    
    let mut iter = nums
        .split(|t| *t == 0)
        .map(|n| nums.subslice_range(n).unwrap());
    
    assert_eq!(iter.next(), Some(0..0));
    assert_eq!(iter.next(), Some(1..3));
    assert_eq!(iter.next(), Some(4..4));
    assert_eq!(iter.next(), Some(5..6));
}
