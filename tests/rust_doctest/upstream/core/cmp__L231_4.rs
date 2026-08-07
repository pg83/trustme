// Extracted from library/core/src/cmp.rs:231
#![allow(unused)]
fn main() {
    let x: u32 = 0;
    let y: u32 = 1;

    assert_eq!(x == y, false);
    assert_eq!(x.eq(&y), false);
}
