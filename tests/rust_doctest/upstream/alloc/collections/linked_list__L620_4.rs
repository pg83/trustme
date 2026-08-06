// Extracted from library/alloc/src/collections/linked_list.rs:620
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    
    let mut dl = LinkedList::new();
    assert!(dl.is_empty());
    
    dl.push_front("foo");
    assert!(!dl.is_empty());
}
