// Extracted from library/alloc/src/string.rs:3532
#![allow(unused)]
extern crate alloc;
fn main() {
    let c: char = 'a';
    let s: String = String::from(c);
    assert_eq!("a", &s[..]);
}
