// Extracted from library/core/src/iter/traits/iterator.rs:3519
#![allow(unused)]
#![feature(iter_array_chunks)]
fn main() {

    let mut iter = "lorem".chars().array_chunks();
    assert_eq!(iter.next(), Some(['l', 'o']));
    assert_eq!(iter.next(), Some(['r', 'e']));
    assert_eq!(iter.next(), None);
    assert_eq!(iter.into_remainder().unwrap().as_slice(), &['m']);
}
