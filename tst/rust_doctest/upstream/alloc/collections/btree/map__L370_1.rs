// Extracted from library/alloc/src/collections/btree/map.rs:370
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::btree_map;
    let iter: btree_map::Iter<'_, u8, u8> = Default::default();
    assert_eq!(iter.len(), 0);
}
