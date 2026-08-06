// Extracted from library/core/src/ops/range.rs:63
#![allow(unused)]
fn main() {
    assert_eq!((3..5), std::ops::Range { start: 3, end: 5 });
    assert_eq!(3 + 4 + 5, (3..6).sum());
}
