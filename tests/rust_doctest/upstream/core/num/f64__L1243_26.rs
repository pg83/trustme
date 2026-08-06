// Extracted from library/core/src/num/f64.rs:1243
#![allow(unused)]
fn main() {
    let value = f64::from_le_bytes([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x40]);
    assert_eq!(value, 12.5);
}
