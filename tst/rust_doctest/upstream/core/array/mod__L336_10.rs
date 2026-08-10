// Extracted from library/core/src/array/mod.rs:336
#![allow(unused)]
fn main() {
    use std::hash::BuildHasher;

    let b = std::hash::RandomState::new();
    let a: [u8; 3] = [0xa8, 0x3c, 0x09];
    let s: &[u8] = &[0xa8, 0x3c, 0x09];
    assert_eq!(b.hash_one(a), b.hash_one(s));
}
