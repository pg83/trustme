// Extracted from library/alloc/src/boxed.rs:20
#![allow(unused)]
extern crate alloc;
fn main() {
    let boxed: Box<u8> = Box::new(5);
    let val: u8 = *boxed;
}
