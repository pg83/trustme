// Extracted from library/alloc/src/string.rs:708
#![allow(unused)]
extern crate alloc;
fn main() {
    // 𝄞music
    let v = &[0xD834, 0xDD1E, 0x006d, 0x0075,
              0x0073, 0x0069, 0x0063];
    assert_eq!(String::from("𝄞music"),
               String::from_utf16(v).unwrap());
    
    // 𝄞mu<invalid>ic
    let v = &[0xD834, 0xDD1E, 0x006d, 0x0075,
              0xD800, 0x0069, 0x0063];
    assert!(String::from_utf16(v).is_err());
}
