// Extracted from library/alloc/src/string.rs:3244
#![allow(unused)]
extern crate alloc;
fn main() {
    let s1 = String::from("hello world");
    let v1 = Vec::from(s1);

    for b in v1 {
        println!("{b}");
    }
}
