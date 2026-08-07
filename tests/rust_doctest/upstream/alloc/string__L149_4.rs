// Extracted from library/alloc/src/string.rs:149
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "hello";
    let third_character = s.chars().nth(2);
    assert_eq!(third_character, Some('l'));

    let s = "💖💖💖💖💖";
    let third_character = s.chars().nth(2);
    assert_eq!(third_character, Some('💖'));
}
