// Extracted from library/alloc/src/string.rs:215
#![allow(unused)]
extern crate alloc;
fn main() {
    fn takes_str(s: &str) { }
    
    let s = String::from("Hello");
    
    takes_str(&s);
}
