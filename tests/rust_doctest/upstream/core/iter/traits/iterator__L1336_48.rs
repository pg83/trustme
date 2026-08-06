// Extracted from library/core/src/iter/traits/iterator.rs:1336
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    
    let mut iter = a.into_iter().take(2);
    
    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), Some(2));
    assert_eq!(iter.next(), None);
}
