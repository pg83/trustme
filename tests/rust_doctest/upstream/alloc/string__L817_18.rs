// Extracted from library/alloc/src/string.rs:817
#![allow(unused)]
#![feature(str_from_utf16_endian)]
extern crate alloc;
fn main() {
    // 𝄞mus<invalid>ic<invalid>
    let v = &[0x34, 0xD8, 0x1E, 0xDD, 0x6d, 0x00, 0x75, 0x00,
              0x73, 0x00, 0x1E, 0xDD, 0x69, 0x00, 0x63, 0x00,
              0x34, 0xD8];

    assert_eq!(String::from("𝄞mus\u{FFFD}ic\u{FFFD}"),
               String::from_utf16le_lossy(v));
}
