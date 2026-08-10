// Extracted from library/core/src/slice/mod.rs:4317
#![allow(unused)]
fn main() {
    assert!([1, 2, 2, 9].is_sorted_by(|a, b| a <= b));
    assert!(![1, 2, 2, 9].is_sorted_by(|a, b| a < b));

    assert!([0].is_sorted_by(|a, b| true));
    assert!([0].is_sorted_by(|a, b| false));

    let empty: [i32; 0] = [];
    assert!(empty.is_sorted_by(|a, b| false));
    assert!(empty.is_sorted_by(|a, b| true));
}
