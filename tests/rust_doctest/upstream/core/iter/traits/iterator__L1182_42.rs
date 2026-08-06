// Extracted from library/core/src/iter/traits/iterator.rs:1182
#![allow(unused)]
fn main() {
    let a = [1, 2, 3, 4];
    let mut iter = a.into_iter();
    
    let result: Vec<i32> = iter.by_ref().take_while(|&n| n != 3).collect();
    
    assert_eq!(result, [1, 2]);
    
    let result: Vec<i32> = iter.collect();
    
    assert_eq!(result, [4]);
}
