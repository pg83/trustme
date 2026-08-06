// Extracted from library/core/src/iter/traits/iterator.rs:2379
#![allow(unused)]
fn main() {
    let a = [10, 20, 30, 100, 40, 50];
    let mut iter = a.into_iter();
    
    // This sum overflows when adding the 100 element
    let sum = iter.try_fold(0i8, |acc, x| acc.checked_add(x));
    assert_eq!(sum, None);
    
    // Because it short-circuited, the remaining elements are still
    // available through the iterator.
    assert_eq!(iter.len(), 2);
    assert_eq!(iter.next(), Some(40));
}
