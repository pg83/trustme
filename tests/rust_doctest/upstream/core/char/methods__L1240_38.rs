// Extracted from library/core/src/char/methods.rs:1240
#![allow(unused)]
fn main() {
    let ascii = 'a';
    let non_ascii = '❤';
    
    assert_eq!('A', ascii.to_ascii_uppercase());
    assert_eq!('❤', non_ascii.to_ascii_uppercase());
}
