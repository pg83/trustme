// Extracted from library/core/src/iter/traits/iterator.rs:1612
#![allow(unused)]
#![feature(iter_map_windows)]
fn main() {

    let mut it = [1, 3, 8, 1].iter().map_windows(|&[a, b]| a + b);
    assert_eq!(it.next(), Some(4));  // 1 + 3
    assert_eq!(it.next(), Some(11)); // 3 + 8
    assert_eq!(it.next(), Some(9));  // 8 + 1
    assert_eq!(it.next(), None);
}
