// Extracted from library/alloc/src/collections/linked_list.rs:1154
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::LinkedList;
    
    let mut numbers: LinkedList<u32> = LinkedList::new();
    numbers.extend(&[1, 2, 3, 4, 5, 6, 8, 9, 11, 13, 14, 15]);
    
    let evens = numbers.extract_if(|x| *x % 2 == 0).collect::<LinkedList<_>>();
    let odds = numbers;
    
    assert_eq!(evens.into_iter().collect::<Vec<_>>(), vec![2, 4, 6, 8, 14]);
    assert_eq!(odds.into_iter().collect::<Vec<_>>(), vec![1, 3, 5, 9, 11, 13, 15]);
}
