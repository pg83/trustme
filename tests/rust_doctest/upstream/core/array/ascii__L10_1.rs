// Extracted from library/core/src/array/ascii.rs:10
#![allow(unused)]
#![feature(ascii_char)]
fn main() {
    
    const HEX_DIGITS: [std::ascii::Char; 16] =
        *b"0123456789abcdef".as_ascii().unwrap();
    
    assert_eq!(HEX_DIGITS[1].as_str(), "1");
    assert_eq!(HEX_DIGITS[10].as_str(), "a");
}
