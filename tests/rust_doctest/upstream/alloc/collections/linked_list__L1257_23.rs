// Extracted from library/alloc/src/collections/linked_list.rs:1257
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::linked_list;
    let iter: linked_list::Iter<'_, u8> = Default::default();
    assert_eq!(iter.len(), 0);
}
