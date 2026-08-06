// Extracted from library/alloc/src/collections/linked_list.rs:1041
#![allow(unused)]
#![feature(linked_list_remove)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    
    let mut d = LinkedList::new();
    
    d.push_front(1);
    d.push_front(2);
    d.push_front(3);
    
    assert_eq!(d.remove(1), 2);
    assert_eq!(d.remove(0), 3);
    assert_eq!(d.remove(0), 1);
}
