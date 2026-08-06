// Extracted from library/alloc/src/boxed.rs:1411
#![allow(unused)]
#![feature(allocator_api, box_vec_non_null)]
extern crate alloc;
fn main() {
    
    use std::alloc::System;
    
    let x = Box::new_in(String::from("Hello"), System);
    let (non_null, alloc) = Box::into_non_null_with_allocator(x);
    let x = unsafe { Box::from_non_null_in(non_null, alloc) };
}
