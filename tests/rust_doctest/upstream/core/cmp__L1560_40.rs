// Extracted from library/core/src/cmp.rs:1560
#![allow(unused)]
fn main() {
    use std::cmp;
    
    let abs_cmp = |x: &i32, y: &i32| x.abs().cmp(&y.abs());
    
    let result = cmp::min_by(2, -1, abs_cmp);
    assert_eq!(result, -1);
    
    let result = cmp::min_by(2, -3, abs_cmp);
    assert_eq!(result, 2);
    
    let result = cmp::min_by(1, -1, abs_cmp);
    assert_eq!(result, 1);
}
