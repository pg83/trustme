// Extracted from library/core/src/iter/traits/iterator.rs:4013
#![allow(unused)]
fn main() {
    assert!(["c", "bb", "aaa"].iter().is_sorted_by_key(|s| s.len()));
    assert!(![-2i32, -1, 0, 3].iter().is_sorted_by_key(|n| n.abs()));
}
