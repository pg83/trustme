// Extracted from library/core/src/iter/adapters/map.rs:22
#![allow(unused)]
fn main() {
    let v: Vec<i32> = [1, 2, 3].into_iter().map(|x| x + 1).rev().collect();

    assert_eq!(v, [4, 3, 2]);
}
