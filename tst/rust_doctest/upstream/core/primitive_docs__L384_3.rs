// Extracted from library/core/src/primitive_docs.rs:384
#![allow(unused)]
fn main() {
    use std::alloc::Layout;
    assert_eq!(Layout::new::<char>(), Layout::new::<u32>());
}
