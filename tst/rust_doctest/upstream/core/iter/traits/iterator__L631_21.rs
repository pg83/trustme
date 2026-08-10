// Extracted from library/core/src/iter/traits/iterator.rs:631
#![allow(unused)]
#![feature(iter_intersperse)]
fn main() {

    let mut a = [0, 1, 2].into_iter().intersperse(100);
    assert_eq!(a.next(), Some(0));   // The first element from `a`.
    assert_eq!(a.next(), Some(100)); // The separator.
    assert_eq!(a.next(), Some(1));   // The next element from `a`.
    assert_eq!(a.next(), Some(100)); // The separator.
    assert_eq!(a.next(), Some(2));   // The last element from `a`.
    assert_eq!(a.next(), None);       // The iterator is finished.
}
