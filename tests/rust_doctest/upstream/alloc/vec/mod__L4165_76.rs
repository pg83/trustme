// Extracted from library/alloc/src/vec/mod.rs:4165
#![allow(unused)]
extern crate alloc;
fn main() {
    let b: Box<[i32]> = vec![1, 2, 3].into_boxed_slice();
    assert_eq!(Vec::from(b), vec![1, 2, 3]);
}
