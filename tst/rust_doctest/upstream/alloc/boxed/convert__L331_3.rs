// Extracted from library/alloc/src/boxed/convert.rs:331
#![allow(unused)]
extern crate alloc;
fn main() {
    let state: Box<[f32; 100]> = vec![1.0; 100].try_into().unwrap();
    assert_eq!(state.len(), 100);
}
