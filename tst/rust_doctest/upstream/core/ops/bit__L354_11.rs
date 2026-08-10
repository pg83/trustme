// Extracted from library/core/src/ops/bit.rs:354
#![allow(unused)]
fn main() {
    assert_eq!(true ^ false, true);
    assert_eq!(true ^ true, false);
    assert_eq!(5u8 ^ 1u8, 4);
    assert_eq!(5u8 ^ 2u8, 7);
}
