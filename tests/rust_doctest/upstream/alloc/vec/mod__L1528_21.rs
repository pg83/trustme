// Extracted from library/alloc/src/vec/mod.rs:1528
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = Vec::with_capacity(10);
    vec.extend([1, 2, 3]);

    assert!(vec.capacity() >= 10);
    let slice = vec.into_boxed_slice();
    assert_eq!(slice.into_vec().capacity(), 3);
}
