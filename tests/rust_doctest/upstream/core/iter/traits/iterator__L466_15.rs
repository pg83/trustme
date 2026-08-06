// Extracted from library/core/src/iter/traits/iterator.rs:466
#![allow(unused)]
fn main() {
    let a1 = [1, 2, 3];
    let a2 = [4, 5, 6];
    
    let mut iter = a1.into_iter().chain(a2);
    
    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), Some(2));
    assert_eq!(iter.next(), Some(3));
    assert_eq!(iter.next(), Some(4));
    assert_eq!(iter.next(), Some(5));
    assert_eq!(iter.next(), Some(6));
    assert_eq!(iter.next(), None);
}
