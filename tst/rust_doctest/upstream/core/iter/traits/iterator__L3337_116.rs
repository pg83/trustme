// Extracted from library/core/src/iter/traits/iterator.rs:3337
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    let mut iter = a.into_iter().rev();

    assert_eq!(iter.next(), Some(3));
    assert_eq!(iter.next(), Some(2));
    assert_eq!(iter.next(), Some(1));

    assert_eq!(iter.next(), None);
}
