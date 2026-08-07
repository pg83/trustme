// Extracted from library/alloc/src/sync.rs:657
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::sync::Arc;
    use std::alloc::System;

    let five = Arc::new_in(5, System);
}
