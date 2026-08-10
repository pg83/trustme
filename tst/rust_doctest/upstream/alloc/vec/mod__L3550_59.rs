// Extracted from library/alloc/src/vec/mod.rs:3550
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::hash::BuildHasher;

    let b = std::hash::RandomState::new();
    let v: Vec<u8> = vec![0xa8, 0x3c, 0x09];
    let s: &[u8] = &[0xa8, 0x3c, 0x09];
    assert_eq!(b.hash_one(v), b.hash_one(s));
}
