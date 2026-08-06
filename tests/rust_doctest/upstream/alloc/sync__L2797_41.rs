// Extracted from library/alloc/src/sync.rs:2797
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::sync::Weak;
    use std::alloc::System;
    
    let empty: Weak<i64, _> = Weak::new_in(System);
    assert!(empty.upgrade().is_none());
}
