// Extracted from library/core/src/iter/adapters/peekable.rs:304
#![allow(unused)]
fn main() {
    let mut iter = (0..5).peekable();
    // The first item of the iterator is 0; consume it.
    assert_eq!(iter.next_if_eq(&0), Some(0));
    // The next item returned is now 1, so `next_if_eq` will return `None`.
    assert_eq!(iter.next_if_eq(&0), None);
    // `next_if_eq` retains the next item if it was not equal to `expected`.
    assert_eq!(iter.next(), Some(1));
}
