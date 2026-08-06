// Extracted from library/alloc/src/collections/linked_list.rs:909
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    
    let mut d = LinkedList::new();
    d.push_back(1);
    d.push_back(3);
    assert_eq!(3, *d.back().unwrap());
}
