// Extracted from library/core/src/ops/bit.rs:836
#![allow(unused)]
fn main() {
    let mut x = true;
    x ^= false;
    assert_eq!(x, true);

    let mut x = true;
    x ^= true;
    assert_eq!(x, false);

    let mut x: u8 = 5;
    x ^= 1;
    assert_eq!(x, 4);

    let mut x: u8 = 5;
    x ^= 2;
    assert_eq!(x, 7);
}
