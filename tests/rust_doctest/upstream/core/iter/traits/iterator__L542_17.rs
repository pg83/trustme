// Extracted from library/core/src/iter/traits/iterator.rs:542
#![allow(unused)]
fn main() {
    let a1 = [1, 2, 3];
    let a2 = [4, 5, 6];
    
    let mut iter = a1.into_iter().zip(a2);
    
    assert_eq!(iter.next(), Some((1, 4)));
    assert_eq!(iter.next(), Some((2, 5)));
    assert_eq!(iter.next(), Some((3, 6)));
    assert_eq!(iter.next(), None);
}
