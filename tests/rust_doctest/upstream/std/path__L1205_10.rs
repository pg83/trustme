// Extracted from library/std/src/path.rs:1205
#![allow(unused)]
fn main() {
    use std::path::PathBuf;

    let mut path = PathBuf::with_capacity(10);
    let capacity = path.capacity();

    // This push is done without reallocating
    path.push(r"C:\");

    assert_eq!(capacity, path.capacity());
}
