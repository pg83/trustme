// Extracted from library/alloc/src/string.rs:1057
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = String::from("foo");
    
    assert_eq!("foo", s.as_str());
}
