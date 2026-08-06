// Extracted from library/alloc/src/collections/linked_list.rs:1087
#![allow(unused)]
#![feature(linked_list_retain)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    
    let mut d = LinkedList::new();
    
    d.push_front(1);
    d.push_front(2);
    d.push_front(3);
    
    d.retain(|&mut x| x % 2 == 0);
    
    assert_eq!(d.pop_front(), Some(2));
    assert_eq!(d.pop_front(), None);
}
