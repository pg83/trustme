// Extracted from library/alloc/src/boxed.rs:1071
#![allow(unused)]
#![feature(box_vec_non_null)]
extern crate alloc;
fn main() {
    
    let x = Box::new(5);
    let non_null = Box::into_non_null(x);
    let x = unsafe { Box::from_non_null(non_null) };
}
