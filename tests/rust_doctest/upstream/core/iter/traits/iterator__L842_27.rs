// Extracted from library/core/src/iter/traits/iterator.rs:842
#![allow(unused)]
fn main() {
    let a = [0i32, 1, 2];
    
    let mut iter = a.into_iter().filter(|x| x.is_positive());
    
    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), Some(2));
    assert_eq!(iter.next(), None);
}
