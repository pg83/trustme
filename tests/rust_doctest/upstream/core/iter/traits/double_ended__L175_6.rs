// Extracted from library/core/src/iter/traits/double_ended.rs:175
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    let mut iter = a.iter();

    assert_eq!(iter.nth_back(1), Some(&2));
    assert_eq!(iter.nth_back(1), None);
}
