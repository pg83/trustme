// Extracted from library/core/src/iter/traits/double_ended.rs:288
#![allow(unused)]
fn main() {
    let numbers = [1, 2, 3, 4, 5];
    
    let zero = "0".to_string();
    
    let result = numbers.iter().rfold(zero, |acc, &x| {
        format!("({x} + {acc})")
    });
    
    assert_eq!(result, "(1 + (2 + (3 + (4 + (5 + 0)))))");
}
