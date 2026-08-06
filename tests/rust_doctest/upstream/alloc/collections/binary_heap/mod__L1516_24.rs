// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1516
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::binary_heap;
    let iter: binary_heap::Iter<'_, u8> = Default::default();
    assert_eq!(iter.len(), 0);
}
