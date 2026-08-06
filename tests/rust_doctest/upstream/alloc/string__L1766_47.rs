// Extracted from library/alloc/src/string.rs:1766
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("bar");
    
    s.insert_str(0, "foo");
    
    assert_eq!("foobar", s);
}
