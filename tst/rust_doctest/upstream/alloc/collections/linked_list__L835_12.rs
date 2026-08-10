// Extracted from library/alloc/src/collections/linked_list.rs:835
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;

    let mut dl = LinkedList::new();

    dl.push_front(2);
    assert_eq!(dl.front().unwrap(), &2);

    dl.push_front(1);
    assert_eq!(dl.front().unwrap(), &1);
}
