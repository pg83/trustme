// Extracted from library/std/src/collections/mod.rs:219
#![allow(unused)]
fn main() {
    let mut vec = vec![1, 2, 3, 4];
    for x in vec.iter_mut() {
       *x += 1;
    }
}
