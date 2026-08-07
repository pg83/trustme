// Extracted from library/alloc/src/vec/mod.rs:4187
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(Box::from(vec![1, 2, 3]), vec![1, 2, 3].into_boxed_slice());
}
