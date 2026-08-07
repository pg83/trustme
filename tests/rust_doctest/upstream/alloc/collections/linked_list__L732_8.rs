// Extracted from library/alloc/src/collections/linked_list.rs:732
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;

    let mut dl = LinkedList::new();
    assert_eq!(dl.front(), None);

    dl.push_front(1);
    assert_eq!(dl.front(), Some(&1));
}
