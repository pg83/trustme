// Extracted from library/alloc/src/string.rs:1630
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("abcde");
    let keep = [false, true, true, false, true];
    let mut iter = keep.iter();
    s.retain(|_| *iter.next().unwrap());
    assert_eq!(s, "bce");
}
