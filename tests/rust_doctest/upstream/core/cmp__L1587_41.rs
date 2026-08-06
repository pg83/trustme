// Extracted from library/core/src/cmp.rs:1587
#![allow(unused)]
fn main() {
    use std::cmp;
    
    let result = cmp::min_by_key(2, -1, |x: &i32| x.abs());
    assert_eq!(result, -1);
    
    let result = cmp::min_by_key(2, -3, |x: &i32| x.abs());
    assert_eq!(result, 2);
    
    let result = cmp::min_by_key(1, -1, |x: &i32| x.abs());
    assert_eq!(result, 1);
}
