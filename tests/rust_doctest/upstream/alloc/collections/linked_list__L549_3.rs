// Extracted from library/alloc/src/collections/linked_list.rs:549
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    
    let mut list: LinkedList<u32> = LinkedList::new();
    
    list.push_back(0);
    list.push_back(1);
    list.push_back(2);
    
    for element in list.iter_mut() {
        *element += 10;
    }
    
    let mut iter = list.iter();
    assert_eq!(iter.next(), Some(&10));
    assert_eq!(iter.next(), Some(&11));
    assert_eq!(iter.next(), Some(&12));
    assert_eq!(iter.next(), None);
}
