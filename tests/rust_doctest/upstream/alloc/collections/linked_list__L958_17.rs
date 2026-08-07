// Extracted from library/alloc/src/collections/linked_list.rs:958
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;

    let mut d = LinkedList::new();
    assert_eq!(d.pop_back(), None);
    d.push_back(1);
    d.push_back(3);
    assert_eq!(d.pop_back(), Some(3));
}
