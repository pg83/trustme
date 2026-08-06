// Extracted from library/core/src/ops/range.rs:239
#![allow(unused)]
fn main() {
    assert_eq!((..5), std::ops::RangeTo { end: 5 });
}
