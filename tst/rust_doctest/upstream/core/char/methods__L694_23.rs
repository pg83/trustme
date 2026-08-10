// Extracted from library/core/src/char/methods.rs:694
#![allow(unused)]
fn main() {
    let mut b = [0; 2];

    let result = 'ß'.encode_utf8(&mut b);

    assert_eq!(result, "ß");

    assert_eq!(result.len(), 2);
}
