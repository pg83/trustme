// Extracted from library/core/src/iter/traits/iterator.rs:679
#![allow(unused)]
#![feature(iter_intersperse)]
fn main() {

    #[derive(PartialEq, Debug)]
    struct NotClone(usize);

    let v = [NotClone(0), NotClone(1), NotClone(2)];
    let mut it = v.into_iter().intersperse_with(|| NotClone(99));

    assert_eq!(it.next(), Some(NotClone(0)));  // The first element from `v`.
    assert_eq!(it.next(), Some(NotClone(99))); // The separator.
    assert_eq!(it.next(), Some(NotClone(1)));  // The next element from `v`.
    assert_eq!(it.next(), Some(NotClone(99))); // The separator.
    assert_eq!(it.next(), Some(NotClone(2)));  // The last element from `v`.
    assert_eq!(it.next(), None);               // The iterator is finished.
}
