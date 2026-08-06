// Extracted from library/core/src/iter/traits/iterator.rs:3101
#![allow(unused)]
fn main() {
    let a = [-1, 2, 3, 4];
    
    let mut iter = a.into_iter();
    
    assert_eq!(iter.rposition(|x| x >= 2), Some(3));
    
    // we can still use `iter`, as there are more elements.
    assert_eq!(iter.next(), Some(-1));
    assert_eq!(iter.next_back(), Some(3));
}
