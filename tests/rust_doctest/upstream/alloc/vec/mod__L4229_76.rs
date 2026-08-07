// Extracted from library/alloc/src/vec/mod.rs:4229
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(vec![1, 2, 3].try_into(), Ok([1, 2, 3]));
    assert_eq!(<Vec<i32>>::new().try_into(), Ok([]));
}
