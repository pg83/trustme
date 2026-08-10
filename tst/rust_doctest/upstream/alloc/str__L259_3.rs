// Extracted from library/alloc/src/str.rs:259
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "this is old";
    assert_eq!(s, s.replace("cookie monster", "little lamb"));
}
