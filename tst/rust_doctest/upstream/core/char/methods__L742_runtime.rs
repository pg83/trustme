// Extracted from library/core/src/char/methods.rs:742
#![allow(unused)]
fn main() {
    let mut b = [0; 1];

    // this panics
    '𝕊'.encode_utf16(&mut b);
}
