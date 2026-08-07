// Extracted from library/std/src/keyword_docs.rs:2396
#![allow(unused)]
fn main() {
    let mut i = 1;

    while i < 100 {
        i *= 2;
        if i == 64 {
            break; // Exit when `i` is 64.
        }
    }
}
