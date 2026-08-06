// Extracted from library/alloc/src/collections/linked_list.rs:507
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::alloc::System;
    use std::collections::LinkedList;
    
    let list: LinkedList<u32, _> = LinkedList::new_in(System);
}
