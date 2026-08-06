// Extracted from library/core/src/primitive_docs.rs:723
#![allow(unused)]
fn main() {
    let tuple: (u32, u32, u32) = (1, 2, 3);
    let array: [u32; 3] = tuple.into();
}
