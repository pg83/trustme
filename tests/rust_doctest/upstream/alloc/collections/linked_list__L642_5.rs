// Extracted from library/alloc/src/collections/linked_list.rs:642
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;

    let mut dl = LinkedList::new();

    dl.push_front(2);
    assert_eq!(dl.len(), 1);

    dl.push_front(1);
    assert_eq!(dl.len(), 2);

    dl.push_back(3);
    assert_eq!(dl.len(), 3);
}
