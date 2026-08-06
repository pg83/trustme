// Extracted from library/core/src/iter/traits/iterator.rs:2575
#![allow(unused)]
fn main() {
    let numbers = [1, 2, 3, 4, 5];
    
    let mut result = 0;
    
    // for loop:
    for i in &numbers {
        result = result + i;
    }
    
    // fold:
    let result2 = numbers.iter().fold(0, |acc, &x| acc + x);
    
    // they're the same
    assert_eq!(result, result2);
}
