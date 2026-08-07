// Extracted from library/alloc/src/string.rs:2027
#![allow(unused)]
#![feature(string_into_chars)]
extern crate alloc;
fn main() {

    let y = String::from("y̆");

    let mut chars = y.into_chars();

    assert_eq!(Some('y'), chars.next()); // not 'y̆'
    assert_eq!(Some('\u{0306}'), chars.next());

    assert_eq!(None, chars.next());
}
