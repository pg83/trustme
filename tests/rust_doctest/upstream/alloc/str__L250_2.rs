// Extracted from library/alloc/src/str.rs:250
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "this is old";

    assert_eq!("this is new", s.replace("old", "new"));
    assert_eq!("than an old", s.replace("is", "an"));
}
