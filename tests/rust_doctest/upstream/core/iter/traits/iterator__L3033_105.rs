// Extracted from library/core/src/iter/traits/iterator.rs:3033
#![allow(unused)]
fn main() {
    let a = [1, 2, 3, 4];
    
    let mut iter = a.into_iter();
    
    assert_eq!(iter.position(|x| x >= 2), Some(1));
    
    // we can still use `iter`, as there are more elements.
    assert_eq!(iter.next(), Some(3));
    
    // The returned index depends on iterator state
    assert_eq!(iter.position(|x| x == 4), Some(0));
}
