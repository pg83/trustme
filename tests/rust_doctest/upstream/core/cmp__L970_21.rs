// Extracted from library/core/src/cmp.rs:970
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    assert_eq!(5.cmp(&10), Ordering::Less);
    assert_eq!(10.cmp(&5), Ordering::Greater);
    assert_eq!(5.cmp(&5), Ordering::Equal);
}
