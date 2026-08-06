// Extracted from library/core/src/ops/range.rs:172
#![allow(unused)]
fn main() {
    assert_eq!((2..), std::ops::RangeFrom { start: 2 });
    assert_eq!(2 + 3 + 4, (2..).take(3).sum());
}
