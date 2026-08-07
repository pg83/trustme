// Extracted from library/core/src/num/mod.rs:628
#![allow(unused)]
fn main() {
    let mut byte = b'A';

    byte.make_ascii_lowercase();

    assert_eq!(b'a', byte);
}
