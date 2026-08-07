// Extracted from library/core/src/array/mod.rs:267
#![allow(unused)]
fn main() {
    let mut bytes: [u8; 3] = [1, 0, 2];

    let bytes_head: [u8; 2] = <[u8; 2]>::try_from(&mut bytes[0..2]).unwrap();
    assert_eq!(1, u16::from_le_bytes(bytes_head));

    let bytes_tail: [u8; 2] = (&mut bytes[1..3]).try_into().unwrap();
    assert_eq!(512, u16::from_le_bytes(bytes_tail));
}
