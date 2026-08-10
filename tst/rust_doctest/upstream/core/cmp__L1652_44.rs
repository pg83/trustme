// Extracted from library/core/src/cmp.rs:1652
#![allow(unused)]
fn main() {
    use std::cmp;

    let abs_cmp = |x: &i32, y: &i32| x.abs().cmp(&y.abs());

    let result = cmp::max_by(3, -2, abs_cmp) ;
    assert_eq!(result, 3);

    let result = cmp::max_by(1, -2, abs_cmp);
    assert_eq!(result, -2);

    let result = cmp::max_by(1, -1, abs_cmp);
    assert_eq!(result, -1);
}
