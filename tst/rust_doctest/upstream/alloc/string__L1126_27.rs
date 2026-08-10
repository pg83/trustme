// Extracted from library/alloc/src/string.rs:1126
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut string = String::from("abcde");

    string.extend_from_within(2..);
    assert_eq!(string, "abcdecde");

    string.extend_from_within(..2);
    assert_eq!(string, "abcdecdeab");

    string.extend_from_within(4..8);
    assert_eq!(string, "abcdecdeabecde");
}
