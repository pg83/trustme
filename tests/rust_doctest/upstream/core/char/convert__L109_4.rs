// Extracted from library/core/src/char/convert.rs:109
#![allow(unused)]
fn main() {
    let a = 'ÿ'; // U+00FF
    let b = 'Ā'; // U+0100
    assert_eq!(u8::try_from(a), Ok(0xFF_u8));
    assert!(u8::try_from(b).is_err());
}
