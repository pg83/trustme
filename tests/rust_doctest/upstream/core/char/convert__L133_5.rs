// Extracted from library/core/src/char/convert.rs:133
#![allow(unused)]
fn main() {
    let trans_rights = '⚧'; // U+26A7
    let ninjas = '🥷'; // U+1F977
    assert_eq!(u16::try_from(trans_rights), Ok(0x26A7_u16));
    assert!(u16::try_from(ninjas).is_err());
}
