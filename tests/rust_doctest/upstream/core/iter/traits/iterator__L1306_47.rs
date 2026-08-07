// Extracted from library/core/src/iter/traits/iterator.rs:1306
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    let mut iter = a.into_iter().skip(2);

    assert_eq!(iter.next(), Some(3));
    assert_eq!(iter.next(), None);
}
