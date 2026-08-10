// Extracted from library/alloc/src/collections/btree/set.rs:1826
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::btree_set;
    let iter: btree_set::Iter<'_, u8> = Default::default();
    assert_eq!(iter.len(), 0);
}
