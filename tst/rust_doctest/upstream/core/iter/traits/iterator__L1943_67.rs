// Extracted from library/core/src/iter/traits/iterator.rs:1943
#![allow(unused)]
fn main() {
    use std::collections::VecDeque;

    let a = [1, 2, 3];

    let doubled: VecDeque<i32> = a.iter().map(|x| x * 2).collect();

    assert_eq!(2, doubled[0]);
    assert_eq!(4, doubled[1]);
    assert_eq!(6, doubled[2]);
}
