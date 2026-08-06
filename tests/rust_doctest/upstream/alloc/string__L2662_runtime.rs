// Extracted from library/alloc/src/string.rs:2662
#![allow(unused)]
extern crate alloc;
fn main() {
    let a = String::from("hello");
    let b = String::from(" world");
    let c = a + &b;
    // `a` is moved and can no longer be used here.
}
