// Extracted from library/alloc/src/vec/mod.rs:4192
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = Vec::with_capacity(10);
    vec.extend([1, 2, 3]);

    assert_eq!(Box::from(vec), vec![1, 2, 3].into_boxed_slice());
}
