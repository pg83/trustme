// Extracted from library/alloc/src/str.rs:458
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "tschüß";

    assert_eq!("TSCHÜSS", s.to_uppercase());
}
