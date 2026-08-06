// Extracted from library/alloc/src/boxed/convert.rs:208
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;
    
    let unboxed = Cow::Borrowed("hello");
    let boxed: Box<str> = Box::from(unboxed);
    println!("{boxed}");
}
