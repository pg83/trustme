// Extracted from library/alloc/src/string.rs:1100
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("foo");
    
    s.push_str("bar");
    
    assert_eq!("foobar", s);
}
