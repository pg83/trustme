// Extracted from library/core/src/iter/traits/iterator.rs:525
#![allow(unused)]
fn main() {
    let s1 = "abc".chars();
    let s2 = "def".chars();

    let mut iter = s1.zip(s2);

    assert_eq!(iter.next(), Some(('a', 'd')));
    assert_eq!(iter.next(), Some(('b', 'e')));
    assert_eq!(iter.next(), Some(('c', 'f')));
    assert_eq!(iter.next(), None);
}
