// Extracted from library/alloc/src/collections/linked_list.rs:2200
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    
    let list1 = LinkedList::from([1, 2, 3, 4]);
    let list2: LinkedList<_> = [1, 2, 3, 4].into();
    assert_eq!(list1, list2);
}
