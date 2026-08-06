// Extracted from library/core/src/iter/traits/iterator.rs:1166
#![allow(unused)]
fn main() {
    let a = [-1, 0, 1, -2];
    
    let mut iter = a.into_iter().take_while(|&x| x < 0);
    
    assert_eq!(iter.next(), Some(-1));
    
    // We have more elements that are less than zero, but since we already
    // got a false, take_while() ignores the remaining elements.
    assert_eq!(iter.next(), None);
}
