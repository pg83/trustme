// Extracted from library/alloc/src/collections/linked_list.rs:524
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;

    let mut list: LinkedList<u32> = LinkedList::new();

    list.push_back(0);
    list.push_back(1);
    list.push_back(2);

    let mut iter = list.iter();
    assert_eq!(iter.next(), Some(&0));
    assert_eq!(iter.next(), Some(&1));
    assert_eq!(iter.next(), Some(&2));
    assert_eq!(iter.next(), None);
}
