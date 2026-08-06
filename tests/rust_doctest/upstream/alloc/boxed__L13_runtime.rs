// Extracted from library/alloc/src/boxed.rs:13
#![allow(unused)]
extern crate alloc;
fn main() {
    let val: u8 = 5;
    let boxed: Box<u8> = Box::new(val);
}
