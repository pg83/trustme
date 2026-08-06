// Extracted from library/core/src/iter/traits/iterator.rs:2534
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    
    // the sum of all of the elements of the array
    let sum = a.iter().fold(0, |acc, x| acc + x);
    
    assert_eq!(sum, 6);
}
