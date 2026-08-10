// Extracted from library/alloc/src/vec/mod.rs:1520
#![allow(unused)]
extern crate alloc;
fn main() {
    let v = vec![1, 2, 3];

    let slice = v.into_boxed_slice();
}
