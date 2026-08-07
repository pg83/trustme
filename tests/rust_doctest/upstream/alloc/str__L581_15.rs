// Extracted from library/alloc/src/str.rs:581
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "Grüße, Jürgen ❤";

    assert_eq!("grüße, jürgen ❤", s.to_ascii_lowercase());
}
