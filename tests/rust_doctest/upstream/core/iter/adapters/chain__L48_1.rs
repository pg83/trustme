// Extracted from library/core/src/iter/adapters/chain.rs:48
#![allow(unused)]
#![feature(iter_chain)]
fn main() {
    
    use std::iter::chain;
    
    let a = [1, 2, 3];
    let b = [4, 5, 6];
    
    let mut iter = chain(a, b);
    
    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), Some(2));
    assert_eq!(iter.next(), Some(3));
    assert_eq!(iter.next(), Some(4));
    assert_eq!(iter.next(), Some(5));
    assert_eq!(iter.next(), Some(6));
    assert_eq!(iter.next(), None);
}
