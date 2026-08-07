// Extracted from library/core/src/num/mod.rs:602
#![allow(unused)]
fn main() {
    let mut byte = b'a';

    byte.make_ascii_uppercase();

    assert_eq!(b'A', byte);
}
