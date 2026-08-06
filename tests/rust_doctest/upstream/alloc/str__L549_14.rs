// Extracted from library/alloc/src/str.rs:549
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "Grüße, Jürgen ❤";
    
    assert_eq!("GRüßE, JüRGEN ❤", s.to_ascii_uppercase());
}
