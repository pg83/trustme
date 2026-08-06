// Extracted from library/core/src/char/methods.rs:188
#![allow(unused)]
fn main() {
    let c = char::from_u32(0x110000);
    
    assert_eq!(None, c);
}
