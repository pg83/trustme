// Extracted from library/core/src/cmp.rs:458
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;

    assert_eq!(Ordering::Less.is_lt(), true);
    assert_eq!(Ordering::Equal.is_lt(), false);
    assert_eq!(Ordering::Greater.is_lt(), false);
}
