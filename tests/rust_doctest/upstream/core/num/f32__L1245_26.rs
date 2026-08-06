// Extracted from library/core/src/num/f32.rs:1245
#![allow(unused)]
fn main() {
    let value = f32::from_le_bytes([0x00, 0x00, 0x48, 0x41]);
    assert_eq!(value, 12.5);
}
