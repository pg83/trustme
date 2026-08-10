// Extracted from library/core/src/num/f32.rs:1172
#![allow(unused)]
fn main() {
    let bytes = 12.5f32.to_le_bytes();
    assert_eq!(bytes, [0x00, 0x00, 0x48, 0x41]);
}
