// Extracted from library/core/src/cmp.rs:439
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;

    assert_eq!(Ordering::Less.is_ne(), true);
    assert_eq!(Ordering::Equal.is_ne(), false);
    assert_eq!(Ordering::Greater.is_ne(), true);
}
