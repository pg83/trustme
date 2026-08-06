// Extracted from library/alloc/src/string.rs:300
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::new();
    
    println!("{}", s.capacity());
    
    for _ in 0..5 {
        s.push_str("hello");
        println!("{}", s.capacity());
    }
}
