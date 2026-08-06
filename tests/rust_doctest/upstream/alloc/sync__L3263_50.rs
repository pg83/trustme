// Extracted from library/alloc/src/sync.rs:3263
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Weak;
    
    let empty: Weak<i64> = Default::default();
    assert!(empty.upgrade().is_none());
}
