// Extracted from library/alloc/src/boxed/convert.rs:216
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;
    let unboxed = Cow::Owned("hello".to_string());
    let boxed: Box<str> = Box::from(unboxed);
    println!("{boxed}");
}
