// Extracted from library/core/src/cmp.rs:515
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    assert_eq!(Ordering::Less.is_ge(), false);
    assert_eq!(Ordering::Equal.is_ge(), true);
    assert_eq!(Ordering::Greater.is_ge(), true);
}
