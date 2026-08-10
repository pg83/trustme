// Extracted from library/alloc/src/boxed.rs:1122
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = Box::new(String::from("Hello"));
    let ptr = Box::into_raw(x);
    let x = unsafe { Box::from_raw(ptr) };
}
