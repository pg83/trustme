// Extracted from library/core/src/cmp.rs:1614
#![allow(unused)]
fn main() {
    use std::cmp;

    assert_eq!(cmp::max(1, 2), 2);
    assert_eq!(cmp::max(2, 2), 2);
}
