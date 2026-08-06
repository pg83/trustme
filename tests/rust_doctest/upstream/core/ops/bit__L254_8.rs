// Extracted from library/core/src/ops/bit.rs:254
#![allow(unused)]
fn main() {
    assert_eq!(true | false, true);
    assert_eq!(false | false, false);
    assert_eq!(5u8 | 1u8, 5);
    assert_eq!(5u8 | 2u8, 7);
}
