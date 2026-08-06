// Extracted from library/core/src/ops/range.rs:538
#![allow(unused)]
fn main() {
    let mut r = 3..=5;
    for _ in r.by_ref() {}
    // Precise field values are unspecified here
    assert!(r.is_empty());
}
