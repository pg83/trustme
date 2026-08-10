// Extracted from library/core/src/iter/traits/iterator.rs:972
#![allow(unused)]
fn main() {
    let a = ['a', 'b', 'c'];

    let mut iter = a.into_iter().enumerate();

    assert_eq!(iter.next(), Some((0, 'a')));
    assert_eq!(iter.next(), Some((1, 'b')));
    assert_eq!(iter.next(), Some((2, 'c')));
    assert_eq!(iter.next(), None);
}
