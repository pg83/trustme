// Extracted from library/core/src/iter/traits/iterator.rs:867
#![allow(unused)]
fn main() {
    let s = &[0, 1, 2];
    
    let mut iter = s.iter().filter(|&x| *x > 1); // both & and *
    
    assert_eq!(iter.next(), Some(&2));
    assert_eq!(iter.next(), None);
}
