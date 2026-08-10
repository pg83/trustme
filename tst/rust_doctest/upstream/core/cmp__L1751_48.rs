// Extracted from library/core/src/cmp.rs:1751
#![allow(unused)]
#![feature(cmp_minmax)]
fn main() {
    use std::cmp;

    let abs_cmp = |x: &i32, y: &i32| x.abs().cmp(&y.abs());

    assert_eq!(cmp::minmax_by(-2, 1, abs_cmp), [1, -2]);
    assert_eq!(cmp::minmax_by(-1, 2, abs_cmp), [-1, 2]);
    assert_eq!(cmp::minmax_by(-2, 2, abs_cmp), [-2, 2]);

    // You can destructure the result using array patterns
    let [min, max] = cmp::minmax_by(-42, 17, abs_cmp);
    assert_eq!(min, 17);
    assert_eq!(max, -42);
}
