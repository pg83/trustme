// Extracted from library/core/src/primitive_docs.rs:861
#![allow(unused)]
fn main() {
    let mut x = [1, 2, 3];
    let x = &mut x[..]; // Take a full slice of `x`.
    x[1] = 7;
    assert_eq!(x, &[1, 7, 3]);
}
