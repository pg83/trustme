// Extracted from library/core/src/cmp.rs:1704
#![allow(unused)]
#![feature(cmp_minmax)]
fn main() {
    use std::cmp;
    
    assert_eq!(cmp::minmax(1, 2), [1, 2]);
    assert_eq!(cmp::minmax(2, 1), [1, 2]);
    
    // You can destructure the result using array patterns
    let [min, max] = cmp::minmax(42, 17);
    assert_eq!(min, 17);
    assert_eq!(max, 42);
}
