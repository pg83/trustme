// Extracted from library/alloc/src/vec/into_iter.rs:92
#![allow(unused)]
extern crate alloc;
fn main() {
    let vec = vec!['a', 'b', 'c'];
    let mut into_iter = vec.into_iter();
    assert_eq!(into_iter.as_slice(), &['a', 'b', 'c']);
    into_iter.as_mut_slice()[2] = 'z';
    assert_eq!(into_iter.next().unwrap(), 'a');
    assert_eq!(into_iter.next().unwrap(), 'b');
    assert_eq!(into_iter.next().unwrap(), 'z');
}
