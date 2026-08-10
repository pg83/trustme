// Extracted from library/alloc/src/str.rs:314
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "this is old";
    assert_eq!(s, s.replacen("cookie monster", "little lamb", 10));
}
