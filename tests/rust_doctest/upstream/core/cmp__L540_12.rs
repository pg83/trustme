// Extracted from library/core/src/cmp.rs:540
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    assert_eq!(Ordering::Less.reverse(), Ordering::Greater);
    assert_eq!(Ordering::Equal.reverse(), Ordering::Equal);
    assert_eq!(Ordering::Greater.reverse(), Ordering::Less);
}
