// Extracted from library/alloc/src/string.rs:749
#![allow(unused)]
extern crate alloc;
fn main() {
    // 𝄞mus<invalid>ic<invalid>
    let v = &[0xD834, 0xDD1E, 0x006d, 0x0075,
              0x0073, 0xDD1E, 0x0069, 0x0063,
              0xD834];

    assert_eq!(String::from("𝄞mus\u{FFFD}ic\u{FFFD}"),
               String::from_utf16_lossy(v));
}
