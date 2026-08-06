// Extracted from library/core/src/char/convert.rs:170
#![allow(unused)]
fn main() {
    let u = 32 as u8;
    let c = char::from(u);
    assert!(4 == size_of_val(&c))
}
