// Extracted from library/core/src/iter/traits/iterator.rs:282
#![allow(unused)]
#![feature(iter_advance_by)]
fn main() {

    use std::num::NonZero;

    let a = [1, 2, 3, 4];
    let mut iter = a.into_iter();

    assert_eq!(iter.advance_by(2), Ok(()));
    assert_eq!(iter.next(), Some(3));
    assert_eq!(iter.advance_by(0), Ok(()));
    assert_eq!(iter.advance_by(100), Err(NonZero::new(99).unwrap())); // only `4` was skipped
}
