// Extracted from library/std/src/keyword_docs.rs:1365
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    struct Node {
        elem: i32,
        // `Self` is a `Node` here.
        next: Option<Box<Self>>,
    }
}
