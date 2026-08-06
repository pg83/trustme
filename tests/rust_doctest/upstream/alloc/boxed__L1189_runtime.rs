// Extracted from library/alloc/src/boxed.rs:1189
#![allow(unused)]
#![feature(box_vec_non_null)]
extern crate alloc;
fn main() {
    
    use std::alloc::{dealloc, Layout};
    
    let x = Box::new(String::from("Hello"));
    let non_null = Box::into_non_null(x);
    unsafe {
        non_null.drop_in_place();
        dealloc(non_null.as_ptr().cast::<u8>(), Layout::new::<String>());
    }
}
