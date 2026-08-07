// Extracted from library/core/src/char/methods.rs:706
#![allow(unused)]
fn main() {
    let mut b = [0; 1];

    // this panics
    'ß'.encode_utf8(&mut b);
}
