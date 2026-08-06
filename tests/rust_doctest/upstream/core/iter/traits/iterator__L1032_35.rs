// Extracted from library/core/src/iter/traits/iterator.rs:1032
#![allow(unused)]
fn main() {
    let xs = [1, 2, 3];
    
    let mut iter = xs.into_iter().peekable();
    
    // `peek_mut()` lets us see into the future
    assert_eq!(iter.peek_mut(), Some(&mut 1));
    assert_eq!(iter.peek_mut(), Some(&mut 1));
    assert_eq!(iter.next(), Some(1));
    
    if let Some(p) = iter.peek_mut() {
        assert_eq!(*p, 2);
        // put a value into the iterator
        *p = 1000;
    }
    
    // The value reappears as the iterator continues
    assert_eq!(iter.collect::<Vec<_>>(), vec![1000, 3]);
}
