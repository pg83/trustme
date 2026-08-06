// Extracted from library/alloc/src/vec/splice.rs:15
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec![0, 1, 2];
    let new = [7, 8];
    let iter: std::vec::Splice<'_, _> = v.splice(1.., new);
}
