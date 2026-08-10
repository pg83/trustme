// Extracted from library/core/src/iter/traits/iterator.rs:2863
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    let mut iter = a.into_iter();

    assert_eq!(iter.find(|&x| x == 2), Some(2));

    // we can still use `iter`, as there are more elements.
    assert_eq!(iter.next(), Some(3));
}
