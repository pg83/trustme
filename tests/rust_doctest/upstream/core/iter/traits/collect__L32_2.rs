// Extracted from library/core/src/iter/traits/collect.rs:32
#![allow(unused)]
fn main() {
    let five_fives = std::iter::repeat(5).take(5);

    let v: Vec<i32> = five_fives.collect();

    assert_eq!(v, vec![5, 5, 5, 5, 5]);
}
