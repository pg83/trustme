// Extracted from library/alloc/src/slice.rs:493
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!([1, 2].repeat(3), vec![1, 2, 1, 2, 1, 2]);
}
