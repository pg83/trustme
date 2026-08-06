// Extracted from library/alloc/src/vec/into_iter.rs:40
#![allow(unused)]
extern crate alloc;
fn main() {
    let v = vec![0, 1, 2];
    let iter: std::vec::IntoIter<_> = v.into_iter();
}
