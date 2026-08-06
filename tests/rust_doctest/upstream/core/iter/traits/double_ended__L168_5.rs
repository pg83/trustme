// Extracted from library/core/src/iter/traits/double_ended.rs:168
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    assert_eq!(a.iter().nth_back(2), Some(&1));
}
