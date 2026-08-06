// Extracted from library/core/src/num/f32.rs:1199
#![allow(unused)]
fn main() {
    let bytes = 12.5f32.to_ne_bytes();
    assert_eq!(
        bytes,
        if cfg!(target_endian = "big") {
            [0x41, 0x48, 0x00, 0x00]
        } else {
            [0x00, 0x00, 0x48, 0x41]
        }
    );
}
