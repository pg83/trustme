// Extracted from library/alloc/src/collections/btree/map.rs:2098
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::btree_map;
    let iter: btree_map::RangeMut<'_, u8, u8> = Default::default();
    assert_eq!(iter.count(), 0);
}
