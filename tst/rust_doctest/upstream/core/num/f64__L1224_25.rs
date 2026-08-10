// Extracted from library/core/src/num/f64.rs:1224
#![allow(unused)]
fn main() {
    let value = f64::from_be_bytes([0x40, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
    assert_eq!(value, 12.5);
}
