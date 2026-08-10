// Extracted from library/alloc/src/string.rs:2007
#![allow(unused)]
#![feature(string_into_chars)]
extern crate alloc;
fn main() {

    let word = String::from("goodbye");

    let mut chars = word.into_chars();

    assert_eq!(Some('g'), chars.next());
    assert_eq!(Some('o'), chars.next());
    assert_eq!(Some('o'), chars.next());
    assert_eq!(Some('d'), chars.next());
    assert_eq!(Some('b'), chars.next());
    assert_eq!(Some('y'), chars.next());
    assert_eq!(Some('e'), chars.next());

    assert_eq!(None, chars.next());
}
