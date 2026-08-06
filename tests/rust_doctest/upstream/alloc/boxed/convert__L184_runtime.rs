// Extracted from library/alloc/src/boxed/convert.rs:184
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut original = String::from("hello");
    let original: &mut str = &mut original;
    let boxed: Box<str> = Box::from(original);
    println!("{boxed}");
}
