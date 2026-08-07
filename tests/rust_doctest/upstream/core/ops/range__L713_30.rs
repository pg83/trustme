// Extracted from library/core/src/ops/range.rs:713
#![allow(unused)]
fn main() {
    use std::ops::Bound::*;

    let bound_string = Included("Hello, World!");

    assert_eq!(bound_string.map(|s| s.len()), Included(13));
}
