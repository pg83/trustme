// Extracted from library/core/src/char/convert.rs:64
#![allow(unused)]
fn main() {
    let c = '👤';
    let u = u64::from(c);
    assert!(8 == size_of_val(&u))
}
