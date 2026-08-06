// Extracted from library/core/src/primitive_docs.rs:705
#![allow(unused)]
fn main() {
    let bytes: [u8; 3] = [1, 0, 2];
    assert_eq!(1, u16::from_le_bytes(<[u8; 2]>::try_from(&bytes[0..2]).unwrap()));
    assert_eq!(512, u16::from_le_bytes(bytes[1..3].try_into().unwrap()));
}
