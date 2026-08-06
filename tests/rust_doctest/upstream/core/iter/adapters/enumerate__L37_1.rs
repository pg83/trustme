// Extracted from library/core/src/iter/adapters/enumerate.rs:37
#![allow(unused)]
#![feature(next_index)]
fn main() {
    
    let arr = ['a', 'b'];
    
    let mut iter = arr.iter().enumerate();
    
    assert_eq!(iter.next_index(), 0);
    assert_eq!(iter.next(), Some((0, &'a')));
    
    assert_eq!(iter.next_index(), 1);
    assert_eq!(iter.next_index(), 1);
    assert_eq!(iter.next(), Some((1, &'b')));
    
    assert_eq!(iter.next_index(), 2);
    assert_eq!(iter.next(), None);
    assert_eq!(iter.next_index(), 2);
}
