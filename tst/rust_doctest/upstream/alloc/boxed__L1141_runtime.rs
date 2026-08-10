// Extracted from library/alloc/src/boxed.rs:1141
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = Box::new(String::from("Hello"));
    let ptr = Box::into_raw(x);
    unsafe {
        drop(Box::from_raw(ptr));
    }
}
