// Extracted from library/core/src/char/methods.rs:1356
#![allow(unused)]
fn main() {
    let mut ascii = 'A';
    
    ascii.make_ascii_lowercase();
    
    assert_eq!('a', ascii);
}
