// Extracted from library/core/src/num/mod.rs:1107
#![allow(unused)]
#![feature(utf16_extra)]
fn main() {
    
    let low_non_surrogate = 0xA000u16;
    let low_surrogate = 0xD800u16;
    let high_surrogate = 0xDC00u16;
    let high_non_surrogate = 0xE000u16;
    
    assert!(!low_non_surrogate.is_utf16_surrogate());
    assert!(low_surrogate.is_utf16_surrogate());
    assert!(high_surrogate.is_utf16_surrogate());
    assert!(!high_non_surrogate.is_utf16_surrogate());
}
