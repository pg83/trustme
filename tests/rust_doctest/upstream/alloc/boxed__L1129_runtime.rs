// Extracted from library/alloc/src/boxed.rs:1129
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::alloc::{dealloc, Layout};
    use std::ptr;

    let x = Box::new(String::from("Hello"));
    let ptr = Box::into_raw(x);
    unsafe {
        ptr::drop_in_place(ptr);
        dealloc(ptr as *mut u8, Layout::new::<String>());
    }
}
