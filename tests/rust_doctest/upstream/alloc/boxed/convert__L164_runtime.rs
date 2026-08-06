// Extracted from library/alloc/src/boxed/convert.rs:164
#![allow(unused)]
extern crate alloc;
fn main() {
    let boxed: Box<str> = Box::from("hello");
    println!("{boxed}");
}
