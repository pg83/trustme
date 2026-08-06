// Extracted from library/alloc/src/string.rs:1841
#![allow(unused)]
extern crate alloc;
fn main() {
    let a = String::from("foo");
    assert_eq!(a.len(), 3);
    
    let fancy_f = String::from("ƒoo");
    assert_eq!(fancy_f.len(), 4);
    assert_eq!(fancy_f.chars().count(), 3);
}
