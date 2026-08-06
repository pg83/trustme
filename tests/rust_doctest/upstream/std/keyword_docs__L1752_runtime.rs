// Extracted from library/std/src/keyword_docs.rs:1752
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    fn from_zero_to(v: u8) -> impl Iterator<Item = u8> {
        (0..v).into_iter()
    }
}
