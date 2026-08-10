// Extracted from library/core/src/num/f64.rs:1197
#![allow(unused)]
fn main() {
    let bytes = 12.5f64.to_ne_bytes();
    assert_eq!(
        bytes,
        if cfg!(target_endian = "big") {
            [0x40, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
        } else {
            [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x40]
        }
    );
}
