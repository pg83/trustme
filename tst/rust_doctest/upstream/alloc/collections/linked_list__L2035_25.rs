// Extracted from library/alloc/src/collections/linked_list.rs:2035
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::linked_list;
    let iter: linked_list::IntoIter<u8> = Default::default();
    assert_eq!(iter.len(), 0);
}
