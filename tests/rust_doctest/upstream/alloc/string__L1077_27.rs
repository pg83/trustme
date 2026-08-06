// Extracted from library/alloc/src/string.rs:1077
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("foobar");
    let s_mut_str = s.as_mut_str();
    
    s_mut_str.make_ascii_uppercase();
    
    assert_eq!("FOOBAR", s_mut_str);
}
