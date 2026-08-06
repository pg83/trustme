// Extracted from library/core/src/char/convert.rs:46
#![allow(unused)]
fn main() {
    let c = 'c';
    let u = u32::from(c);
    assert!(4 == size_of_val(&u))
}
