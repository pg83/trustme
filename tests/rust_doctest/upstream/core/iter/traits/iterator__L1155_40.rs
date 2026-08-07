// Extracted from library/core/src/iter/traits/iterator.rs:1155
#![allow(unused)]
fn main() {
    let s = &[-1, 0, 1];

    let mut iter = s.iter().take_while(|x| **x < 0); // need two *s!

    assert_eq!(iter.next(), Some(&-1));
    assert_eq!(iter.next(), None);
}
