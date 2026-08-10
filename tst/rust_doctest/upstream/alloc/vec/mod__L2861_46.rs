// Extracted from library/alloc/src/vec/mod.rs:2861
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = Vec::new();
    assert!(v.is_empty());

    v.push(1);
    assert!(!v.is_empty());
}
