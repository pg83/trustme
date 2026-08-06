// Extracted from library/core/src/cmp.rs:376
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    assert_eq!(1.cmp(&2), Ordering::Less);
    
    assert_eq!(1.cmp(&1), Ordering::Equal);
    
    assert_eq!(2.cmp(&1), Ordering::Greater);
}
