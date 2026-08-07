// Extracted from library/alloc/src/string.rs:1398
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("abc");

    s.push('1');
    s.push('2');
    s.push('3');

    assert_eq!("abc123", s);
}
