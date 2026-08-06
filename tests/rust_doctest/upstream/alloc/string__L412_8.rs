// Extracted from library/alloc/src/string.rs:412
#![allow(unused)]
extern crate alloc;
fn main() {
    // 𝄞mu<invalid>ic
    let v = &[0xD834, 0xDD1E, 0x006d, 0x0075,
              0xD800, 0x0069, 0x0063];
    
    assert!(String::from_utf16(v).is_err());
}
