// Extracted from library/alloc/src/vec/mod.rs:47
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec![1, 2, 3];
    let three = v[2];
    v[1] = v[1] + 5;
}
