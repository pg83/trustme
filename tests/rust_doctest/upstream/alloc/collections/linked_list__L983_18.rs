// Extracted from library/alloc/src/collections/linked_list.rs:983
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;

    let mut d = LinkedList::new();

    d.push_front(1);
    d.push_front(2);
    d.push_front(3);

    let mut split = d.split_off(2);

    assert_eq!(split.pop_front(), Some(1));
    assert_eq!(split.pop_front(), None);
}
