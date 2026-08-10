// Extracted from library/alloc/src/collections/btree/map.rs:2211
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::btree_map;
    let iter: btree_map::IntoKeys<u8, u8> = Default::default();
    assert_eq!(iter.len(), 0);
}
