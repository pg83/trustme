// Extracted from library/alloc/src/vec/mod.rs:4091
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(Vec::from(&[1, 2, 3]), vec![1, 2, 3]);
}
