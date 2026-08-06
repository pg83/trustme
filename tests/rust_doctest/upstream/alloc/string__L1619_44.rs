// Extracted from library/alloc/src/string.rs:1619
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("f_o_ob_ar");
    
    s.retain(|c| c != '_');
    
    assert_eq!(s, "foobar");
}
