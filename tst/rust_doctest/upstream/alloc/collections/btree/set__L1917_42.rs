// Extracted from library/alloc/src/collections/btree/set.rs:1917
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::btree_set;
    let iter: btree_set::Range<'_, u8> = Default::default();
    assert_eq!(iter.count(), 0);
}
