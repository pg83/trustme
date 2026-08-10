// Extracted from library/core/src/cmp.rs:1397
#![allow(unused)]
fn main() {
    assert_eq!(1.0 <= 1.0, true);
    assert_eq!(1.0 <= 2.0, true);
    assert_eq!(2.0 <= 1.0, false);
}
