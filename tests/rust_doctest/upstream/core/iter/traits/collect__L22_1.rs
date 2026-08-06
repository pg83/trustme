// Extracted from library/core/src/iter/traits/collect.rs:22
#![allow(unused)]
fn main() {
    let five_fives = std::iter::repeat(5).take(5);
    
    let v = Vec::from_iter(five_fives);
    
    assert_eq!(v, vec![5, 5, 5, 5, 5]);
}
