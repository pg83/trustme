// Extracted from library/alloc/src/string.rs:1481
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("abč");

    assert_eq!(s.pop(), Some('č'));
    assert_eq!(s.pop(), Some('b'));
    assert_eq!(s.pop(), Some('a'));

    assert_eq!(s.pop(), None);
}
