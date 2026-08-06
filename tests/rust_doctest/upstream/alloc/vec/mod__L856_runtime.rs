// Extracted from library/alloc/src/vec/mod.rs:856
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::alloc::System;
    
    #[allow(unused_mut)]
    let mut vec: Vec<i32, _> = Vec::new_in(System);
}
