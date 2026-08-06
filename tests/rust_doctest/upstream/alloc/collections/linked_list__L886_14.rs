// Extracted from library/alloc/src/collections/linked_list.rs:886
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    
    let mut d = LinkedList::new();
    assert_eq!(d.pop_front(), None);
    
    d.push_front(1);
    d.push_front(3);
    assert_eq!(d.pop_front(), Some(3));
    assert_eq!(d.pop_front(), Some(1));
    assert_eq!(d.pop_front(), None);
}
