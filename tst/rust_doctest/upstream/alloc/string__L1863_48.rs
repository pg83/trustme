// Extracted from library/alloc/src/string.rs:1863
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = String::new();
    assert!(v.is_empty());

    v.push('a');
    assert!(!v.is_empty());
}
