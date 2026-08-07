// Extracted from library/core/src/iter/traits/iterator.rs:413
#![allow(unused)]
fn main() {
    let a = [0, 1, 2, 3, 4, 5];
    let mut iter = a.into_iter().step_by(2);

    assert_eq!(iter.next(), Some(0));
    assert_eq!(iter.next(), Some(2));
    assert_eq!(iter.next(), Some(4));
    assert_eq!(iter.next(), None);
}
