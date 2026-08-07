// Extracted from library/alloc/src/vec/mod.rs:4075
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(Vec::from(&mut [1, 2, 3][..]), vec![1, 2, 3]);
}
