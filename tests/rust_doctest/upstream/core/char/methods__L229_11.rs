// Extracted from library/core/src/char/methods.rs:229
#![allow(unused)]
fn main() {
    let c = unsafe { char::from_u32_unchecked(0x2764) };

    assert_eq!('❤', c);
}
