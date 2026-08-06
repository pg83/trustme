// Extracted from library/core/src/cmp.rs:496
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    assert_eq!(Ordering::Less.is_le(), true);
    assert_eq!(Ordering::Equal.is_le(), true);
    assert_eq!(Ordering::Greater.is_le(), false);
}
