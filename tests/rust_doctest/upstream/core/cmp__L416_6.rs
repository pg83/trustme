// Extracted from library/core/src/cmp.rs:416
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    assert_eq!(Ordering::Less.is_eq(), false);
    assert_eq!(Ordering::Equal.is_eq(), true);
    assert_eq!(Ordering::Greater.is_eq(), false);
}
