// Extracted from library/core/src/iter/traits/exact_size.rs:39
#![allow(unused)]
fn main() {
    // a finite range knows exactly how many times it will iterate
    let five = 0..5;

    assert_eq!(5, five.len());
}
