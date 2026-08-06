// Extracted from library/alloc/src/vec/into_iter.rs:446
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::vec;
    let iter: vec::IntoIter<u8> = Default::default();
    assert_eq!(iter.len(), 0);
    assert_eq!(iter.as_slice(), &[]);
}
