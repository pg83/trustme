// Extracted from library/core/src/iter/traits/iterator.rs:446
#![allow(unused)]
fn main() {
    let s1 = "abc".chars();
    let s2 = "def".chars();
    
    let mut iter = s1.chain(s2);
    
    assert_eq!(iter.next(), Some('a'));
    assert_eq!(iter.next(), Some('b'));
    assert_eq!(iter.next(), Some('c'));
    assert_eq!(iter.next(), Some('d'));
    assert_eq!(iter.next(), Some('e'));
    assert_eq!(iter.next(), Some('f'));
    assert_eq!(iter.next(), None);
}
