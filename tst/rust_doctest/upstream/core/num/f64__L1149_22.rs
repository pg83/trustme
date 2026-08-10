// Extracted from library/core/src/num/f64.rs:1149
#![allow(unused)]
fn main() {
    let bytes = 12.5f64.to_be_bytes();
    assert_eq!(bytes, [0x40, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
}
