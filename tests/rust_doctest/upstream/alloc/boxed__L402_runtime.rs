// Extracted from library/alloc/src/boxed.rs:402
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::alloc::System;
    
    let five = Box::new_in(5, System);
}
