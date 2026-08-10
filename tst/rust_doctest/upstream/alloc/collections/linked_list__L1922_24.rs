// Extracted from library/alloc/src/collections/linked_list.rs:1922
#![allow(unused)]
#![feature(linked_list_cursors)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    let mut dl = LinkedList::new();
    dl.push_front(3);
    dl.push_front(2);
    dl.push_front(1);
    let mut cursor = dl.cursor_front_mut();
    *cursor.current().unwrap() = 99;
    *cursor.back_mut().unwrap() = 0;
    let mut contents = dl.into_iter();
    assert_eq!(contents.next(), Some(99));
    assert_eq!(contents.next(), Some(2));
    assert_eq!(contents.next(), Some(0));
    assert_eq!(contents.next(), None);
}
