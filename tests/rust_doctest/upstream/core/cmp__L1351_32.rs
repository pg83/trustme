// Extracted from library/core/src/cmp.rs:1351
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    let result = 1.0.partial_cmp(&2.0);
    assert_eq!(result, Some(Ordering::Less));
    
    let result = 1.0.partial_cmp(&1.0);
    assert_eq!(result, Some(Ordering::Equal));
    
    let result = 2.0.partial_cmp(&1.0);
    assert_eq!(result, Some(Ordering::Greater));
}
