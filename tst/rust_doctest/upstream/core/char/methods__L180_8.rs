// Extracted from library/core/src/char/methods.rs:180
#![allow(unused)]
fn main() {
    let c = char::from_u32(0x2764);

    assert_eq!(Some('❤'), c);
}
