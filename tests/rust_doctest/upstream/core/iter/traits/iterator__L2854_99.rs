// Extracted from library/core/src/iter/traits/iterator.rs:2854
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    assert_eq!(a.into_iter().find(|&x| x == 2), Some(2));
    assert_eq!(a.into_iter().find(|&x| x == 5), None);
}
