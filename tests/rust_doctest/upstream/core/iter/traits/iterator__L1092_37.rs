// Extracted from library/core/src/iter/traits/iterator.rs:1092
#![allow(unused)]
fn main() {
    let s = &[-1, 0, 1];
    
    let mut iter = s.iter().skip_while(|x| **x < 0); // need two *s!
    
    assert_eq!(iter.next(), Some(&0));
    assert_eq!(iter.next(), Some(&1));
    assert_eq!(iter.next(), None);
}
