// Extracted from library/core/src/iter/traits/iterator.rs:1625
#![allow(unused)]
#![feature(iter_map_windows)]
fn main() {
    
    let mut it = "ferris".chars().map_windows(|w: &[_; 3]| *w);
    assert_eq!(it.next(), Some(['f', 'e', 'r']));
    assert_eq!(it.next(), Some(['e', 'r', 'r']));
    assert_eq!(it.next(), Some(['r', 'r', 'i']));
    assert_eq!(it.next(), Some(['r', 'i', 's']));
    assert_eq!(it.next(), None);
}
