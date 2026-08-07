// Extracted from library/core/src/cmp.rs:1522
#![allow(unused)]
fn main() {
    use std::cmp;

    assert_eq!(cmp::min(1, 2), 1);
    assert_eq!(cmp::min(2, 2), 2);
}
