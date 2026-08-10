// Extracted from library/core/src/num/f32.rs:1151
#![allow(unused)]
fn main() {
    let bytes = 12.5f32.to_be_bytes();
    assert_eq!(bytes, [0x41, 0x48, 0x00, 0x00]);
}
