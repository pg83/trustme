// Extracted from library/core/src/iter/traits/iterator.rs:1360
#![allow(unused)]
fn main() {
    let v = [1, 2];
    let mut iter = v.into_iter().take(5);
    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), Some(2));
    assert_eq!(iter.next(), None);
}
