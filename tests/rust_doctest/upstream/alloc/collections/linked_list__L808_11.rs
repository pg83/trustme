// Extracted from library/alloc/src/collections/linked_list.rs:808
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;

    let mut dl = LinkedList::new();
    assert_eq!(dl.back(), None);

    dl.push_back(1);
    assert_eq!(dl.back(), Some(&1));

    match dl.back_mut() {
        None => {},
        Some(x) => *x = 5,
    }
    assert_eq!(dl.back(), Some(&5));
}
