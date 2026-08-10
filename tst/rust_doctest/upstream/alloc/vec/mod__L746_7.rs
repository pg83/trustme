// Extracted from library/alloc/src/vec/mod.rs:746
#![allow(unused)]
#![feature(vec_peek_mut)]
extern crate alloc;
fn main() {
    let mut vec = Vec::new();
    assert!(vec.peek_mut().is_none());

    vec.push(1);
    vec.push(5);
    vec.push(2);
    assert_eq!(vec.last(), Some(&2));
    if let Some(mut val) = vec.peek_mut() {
        *val = 0;
    }
    assert_eq!(vec.last(), Some(&0));
}
