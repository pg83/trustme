// Extracted from library/alloc/src/boxed.rs:1422
#![allow(unused)]
#![feature(allocator_api, box_vec_non_null)]
extern crate alloc;
fn main() {

    use std::alloc::{Allocator, Layout, System};

    let x = Box::new_in(String::from("Hello"), System);
    let (non_null, alloc) = Box::into_non_null_with_allocator(x);
    unsafe {
        non_null.drop_in_place();
        alloc.deallocate(non_null.cast::<u8>(), Layout::new::<String>());
    }
}
