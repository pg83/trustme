// Extracted from library/core/src/ops/range.rs:522
#![allow(unused)]
fn main() {
    assert!(!(3..=5).is_empty());
    assert!(!(3..=3).is_empty());
    assert!( (3..=2).is_empty());
}
