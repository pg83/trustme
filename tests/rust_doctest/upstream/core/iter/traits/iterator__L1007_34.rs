// Extracted from library/core/src/iter/traits/iterator.rs:1007
#![allow(unused)]
fn main() {
    let xs = [1, 2, 3];
    
    let mut iter = xs.into_iter().peekable();
    
    // peek() lets us see into the future
    assert_eq!(iter.peek(), Some(&1));
    assert_eq!(iter.next(), Some(1));
    
    assert_eq!(iter.next(), Some(2));
    
    // we can peek() multiple times, the iterator won't advance
    assert_eq!(iter.peek(), Some(&3));
    assert_eq!(iter.peek(), Some(&3));
    
    assert_eq!(iter.next(), Some(3));
    
    // after the iterator is finished, so is peek()
    assert_eq!(iter.peek(), None);
    assert_eq!(iter.next(), None);
}
