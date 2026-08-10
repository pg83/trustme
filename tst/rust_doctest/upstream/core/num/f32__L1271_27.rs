// Extracted from library/core/src/num/f32.rs:1271
#![allow(unused)]
fn main() {
    let value = f32::from_ne_bytes(if cfg!(target_endian = "big") {
        [0x41, 0x48, 0x00, 0x00]
    } else {
        [0x00, 0x00, 0x48, 0x41]
    });
    assert_eq!(value, 12.5);
}
