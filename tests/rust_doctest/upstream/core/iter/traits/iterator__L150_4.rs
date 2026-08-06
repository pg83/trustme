// Extracted from library/core/src/iter/traits/iterator.rs:150
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    let mut iter = a.iter();
    
    assert_eq!((3, Some(3)), iter.size_hint());
    let _ = iter.next();
    assert_eq!((2, Some(2)), iter.size_hint());
}
