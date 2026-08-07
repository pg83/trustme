// Extracted from library/core/src/iter/adapters/peekable.rs:238
#![allow(unused)]
fn main() {
    let mut iter = [1, 2, 3].iter().peekable();

    // Like with `peek()`, we can see into the future without advancing the iterator.
    assert_eq!(iter.peek_mut(), Some(&mut &1));
    assert_eq!(iter.peek_mut(), Some(&mut &1));
    assert_eq!(iter.next(), Some(&1));

    // Peek into the iterator and set the value behind the mutable reference.
    if let Some(p) = iter.peek_mut() {
        assert_eq!(*p, &2);
        *p = &5;
    }

    // The value we put in reappears as the iterator continues.
    assert_eq!(iter.collect::<Vec<_>>(), vec![&5, &3]);
}
