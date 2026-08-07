// Extracted from library/alloc/src/collections/linked_list.rs:929
#![allow(unused)]
#![feature(push_mut)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;

    let mut dl = LinkedList::from([1, 2, 3]);

    let ptr = dl.push_back_mut(2);
    *ptr += 4;
    assert_eq!(dl.back().unwrap(), &6);
}
