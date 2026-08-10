// Extracted from library/alloc/src/vec/drain.rs:18
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec![0, 1, 2];
    let iter: std::vec::Drain<'_, _> = v.drain(..);
}
