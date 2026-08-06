// Extracted from library/core/src/ops/range.rs:440
#![allow(unused)]
fn main() {
    assert_eq!((3..=5).into_inner(), (3, 5));
}
