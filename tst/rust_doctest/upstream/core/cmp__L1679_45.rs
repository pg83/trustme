// Extracted from library/core/src/cmp.rs:1679
#![allow(unused)]
fn main() {
    use std::cmp;

    let result = cmp::max_by_key(3, -2, |x: &i32| x.abs());
    assert_eq!(result, 3);

    let result = cmp::max_by_key(1, -2, |x: &i32| x.abs());
    assert_eq!(result, -2);

    let result = cmp::max_by_key(1, -1, |x: &i32| x.abs());
    assert_eq!(result, -1);
}
