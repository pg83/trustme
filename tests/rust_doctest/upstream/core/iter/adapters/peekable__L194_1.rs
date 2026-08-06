// Extracted from library/core/src/iter/adapters/peekable.rs:194
#![allow(unused)]
fn main() {
    let xs = [1, 2, 3];
    
    let mut iter = xs.iter().peekable();
    
    // peek() lets us see into the future
    assert_eq!(iter.peek(), Some(&&1));
    assert_eq!(iter.next(), Some(&1));
    
    assert_eq!(iter.next(), Some(&2));
    
    // The iterator does not advance even if we `peek` multiple times
    assert_eq!(iter.peek(), Some(&&3));
    assert_eq!(iter.peek(), Some(&&3));
    
    assert_eq!(iter.next(), Some(&3));
    
    // After the iterator is finished, so is `peek()`
    assert_eq!(iter.peek(), None);
    assert_eq!(iter.next(), None);
}
