// Extracted from library/core/src/char/methods.rs:732
#![allow(unused)]
fn main() {
    let mut b = [0; 2];
    
    let result = '𝕊'.encode_utf16(&mut b);
    
    assert_eq!(result.len(), 2);
}
