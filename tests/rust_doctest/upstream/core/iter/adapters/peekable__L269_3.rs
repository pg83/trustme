// Extracted from library/core/src/iter/adapters/peekable.rs:269
#![allow(unused)]
fn main() {
    let mut iter = (0..5).peekable();
    // The first item of the iterator is 0; consume it.
    assert_eq!(iter.next_if(|&x| x == 0), Some(0));
    // The next item returned is now 1, so `next_if` will return `None`.
    assert_eq!(iter.next_if(|&x| x == 0), None);
    // `next_if` retains the next item if the predicate evaluates to `false` for it.
    assert_eq!(iter.next(), Some(1));
}
