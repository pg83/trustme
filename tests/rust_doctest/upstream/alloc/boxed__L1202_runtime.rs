// Extracted from library/alloc/src/boxed.rs:1202
#![allow(unused)]
#![feature(box_vec_non_null)]
extern crate alloc;
fn main() {
    
    let x = Box::new(String::from("Hello"));
    let non_null = Box::into_non_null(x);
    unsafe {
        drop(Box::from_non_null(non_null));
    }
}
