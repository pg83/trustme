// Extracted from library/core/src/cmp.rs:1782
#![allow(unused)]
#![feature(cmp_minmax)]
fn main() {
    use std::cmp;

    assert_eq!(cmp::minmax_by_key(-2, 1, |x: &i32| x.abs()), [1, -2]);
    assert_eq!(cmp::minmax_by_key(-2, 2, |x: &i32| x.abs()), [-2, 2]);

    // You can destructure the result using array patterns
    let [min, max] = cmp::minmax_by_key(-42, 17, |x: &i32| x.abs());
    assert_eq!(min, 17);
    assert_eq!(max, -42);
}
