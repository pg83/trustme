// Extracted from library/core/src/iter/traits/iterator.rs:2558
#![allow(unused)]
fn main() {
    let numbers = [1, 2, 3, 4, 5];
    
    let zero = "0".to_string();
    
    let result = numbers.iter().fold(zero, |acc, &x| {
        format!("({acc} + {x})")
    });
    
    assert_eq!(result, "(((((0 + 1) + 2) + 3) + 4) + 5)");
}
