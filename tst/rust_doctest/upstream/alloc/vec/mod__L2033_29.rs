// Extracted from library/alloc/src/vec/mod.rs:2033
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec!['a', 'b', 'c'];
    vec.insert(1, 'd');
    assert_eq!(vec, ['a', 'd', 'b', 'c']);
    vec.insert(4, 'e');
    assert_eq!(vec, ['a', 'd', 'b', 'c', 'e']);
}
