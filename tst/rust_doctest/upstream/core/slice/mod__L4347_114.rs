// Extracted from library/core/src/slice/mod.rs:4347
#![allow(unused)]
fn main() {
    assert!(["c", "bb", "aaa"].is_sorted_by_key(|s| s.len()));
    assert!(![-2i32, -1, 0, 3].is_sorted_by_key(|n| n.abs()));
}
