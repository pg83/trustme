// Extracted from library/core/src/iter/traits/collect.rs:43
#![allow(unused)]
fn main() {
    use std::collections::VecDeque;
    let first = (0..10).collect::<VecDeque<i32>>();
    let second = VecDeque::from_iter(0..10);

    assert_eq!(first, second);
}
