// Extracted from library/core/src/str/iter.rs:251
#![allow(unused)]
fn main() {
    let mut chars = "a楽".char_indices();
    
    // `next()` has not been called yet, so `offset()` returns the byte
    // index of the first character of the string, which is always 0.
    assert_eq!(chars.offset(), 0);
    // As expected, the first call to `next()` also returns 0 as index.
    assert_eq!(chars.next(), Some((0, 'a')));
    
    // `next()` has been called once, so `offset()` returns the byte index
    // of the second character ...
    assert_eq!(chars.offset(), 1);
    // ... which matches the index returned by the next call to `next()`.
    assert_eq!(chars.next(), Some((1, '楽')));
    
    // Once the iterator has been consumed, `offset()` returns the length
    // in bytes of the string.
    assert_eq!(chars.offset(), 4);
    assert_eq!(chars.next(), None);
}
