// Extracted from library/alloc/src/string.rs:90
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut hello = String::from("Hello, ");

    hello.push('w');
    hello.push_str("orld!");
}
