// Extracted from library/core/src/ops/range.rs:501
#![allow(unused)]
fn main() {
    let mut r = 3..=5;
    assert!(r.contains(&3) && r.contains(&5));
    for _ in r.by_ref() {}
    // Precise field values are unspecified here
    assert!(!r.contains(&3) && !r.contains(&5));
}
