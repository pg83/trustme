// Extracted from library/core/src/cmp.rs:477
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    assert_eq!(Ordering::Less.is_gt(), false);
    assert_eq!(Ordering::Equal.is_gt(), false);
    assert_eq!(Ordering::Greater.is_gt(), true);
}
