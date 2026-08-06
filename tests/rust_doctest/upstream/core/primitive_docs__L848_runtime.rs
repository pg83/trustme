// Extracted from library/core/src/primitive_docs.rs:848
#![allow(unused)]
fn main() {
    // slicing a Vec
    let vec = vec![1, 2, 3];
    let int_slice = &vec[..];
    // coercing an array to a slice
    let str_slice: &[&str] = &["one", "two", "three"];
}
