// Extracted from library/core/src/num/f32.rs:1226
#![allow(unused)]
fn main() {
    let value = f32::from_be_bytes([0x41, 0x48, 0x00, 0x00]);
    assert_eq!(value, 12.5);
}
