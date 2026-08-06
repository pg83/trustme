// Extracted from library/core/src/cmp.rs:657
#![allow(unused)]
fn main() {
    use std::cmp::Reverse;
    
    let mut v = vec![1, 2, 3, 4, 5, 6];
    v.sort_by_key(|&num| (num > 3, Reverse(num)));
    assert_eq!(v, vec![3, 2, 1, 6, 5, 4]);
}
