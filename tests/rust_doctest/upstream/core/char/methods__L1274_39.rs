// Extracted from library/core/src/char/methods.rs:1274
#![allow(unused)]
fn main() {
    let ascii = 'A';
    let non_ascii = '❤';
    
    assert_eq!('a', ascii.to_ascii_lowercase());
    assert_eq!('❤', non_ascii.to_ascii_lowercase());
}
