// Extracted from library/alloc/src/string.rs:1514
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("abç");

    assert_eq!(s.remove(0), 'a');
    assert_eq!(s.remove(1), 'ç');
    assert_eq!(s.remove(0), 'b');
}
