// Extracted from library/core/src/iter/traits/iterator.rs:1078
#![allow(unused)]
fn main() {
    let a = [-1i32, 0, 1];
    
    let mut iter = a.into_iter().skip_while(|x| x.is_negative());
    
    assert_eq!(iter.next(), Some(0));
    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), None);
}
