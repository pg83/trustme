// Extracted from library/alloc/src/slice.rs:361
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = [10, 40, 30];
    let x = s.to_vec();
    // Here, `s` and `x` can be modified independently.
}
