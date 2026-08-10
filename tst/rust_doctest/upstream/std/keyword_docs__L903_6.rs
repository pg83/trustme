// Extracted from library/std/src/keyword_docs.rs:903
#![allow(unused)]
fn main() {
    let mut i = 1;
    let something = loop {
        i *= 2;
        if i > 100 {
            break i;
        }
    };
    assert_eq!(something, 128);
}
