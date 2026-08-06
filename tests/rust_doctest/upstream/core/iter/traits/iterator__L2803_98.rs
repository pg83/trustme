// Extracted from library/core/src/iter/traits/iterator.rs:2803
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    
    let mut iter = a.into_iter();
    
    assert!(iter.any(|x| x != 2));
    
    // we can still use `iter`, as there are more elements.
    assert_eq!(iter.next(), Some(2));
}
