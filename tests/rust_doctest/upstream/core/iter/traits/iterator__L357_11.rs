// Extracted from library/core/src/iter/traits/iterator.rs:357
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    
    let mut iter = a.into_iter();
    
    assert_eq!(iter.nth(1), Some(2));
    assert_eq!(iter.nth(1), None);
}
