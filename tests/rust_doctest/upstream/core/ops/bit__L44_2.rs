// Extracted from library/core/src/ops/bit.rs:44
#![allow(unused)]
fn main() {
    assert_eq!(!true, false);
    assert_eq!(!false, true);
    assert_eq!(!1u8, 254);
    assert_eq!(!0u8, 255);
}
