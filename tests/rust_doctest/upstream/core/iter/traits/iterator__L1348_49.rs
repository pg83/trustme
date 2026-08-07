// Extracted from library/core/src/iter/traits/iterator.rs:1348
#![allow(unused)]
fn main() {
    let mut iter = (0..).take(3);

    assert_eq!(iter.next(), Some(0));
    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), Some(2));
    assert_eq!(iter.next(), None);
}
