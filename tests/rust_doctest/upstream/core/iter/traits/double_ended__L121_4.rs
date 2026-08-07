// Extracted from library/core/src/iter/traits/double_ended.rs:121
#![allow(unused)]
#![feature(iter_advance_by)]
fn main() {

    use std::num::NonZero;

    let a = [3, 4, 5, 6];
    let mut iter = a.iter();

    assert_eq!(iter.advance_back_by(2), Ok(()));
    assert_eq!(iter.next_back(), Some(&4));
    assert_eq!(iter.advance_back_by(0), Ok(()));
    assert_eq!(iter.advance_back_by(100), Err(NonZero::new(99).unwrap())); // only `&3` was skipped
}
