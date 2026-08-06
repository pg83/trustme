// Extracted from library/alloc/src/string.rs:2671
#![allow(unused)]
extern crate alloc;
fn main() {
    let a = String::from("hello");
    let b = String::from(" world");
    let c = a.clone() + &b;
    // `a` is still valid here.
}
