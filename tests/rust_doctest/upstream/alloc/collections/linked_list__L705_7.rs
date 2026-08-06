// Extracted from library/alloc/src/collections/linked_list.rs:705
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    
    let mut list: LinkedList<u32> = LinkedList::new();
    
    list.push_back(0);
    list.push_back(1);
    list.push_back(2);
    
    assert_eq!(list.contains(&0), true);
    assert_eq!(list.contains(&10), false);
}
