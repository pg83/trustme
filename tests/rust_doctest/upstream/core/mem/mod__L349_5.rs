// Extracted from library/core/src/mem/mod.rs:349
#![allow(unused)]
fn main() {
    assert_eq!(4, size_of_val(&5i32));

    let x: [u8; 13] = [0; 13];
    let y: &[u8] = &x;
    assert_eq!(13, size_of_val(y));
}
