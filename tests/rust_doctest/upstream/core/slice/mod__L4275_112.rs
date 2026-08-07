// Extracted from library/core/src/slice/mod.rs:4275
#![allow(unused)]
fn main() {
    let empty: [i32; 0] = [];

    assert!([1, 2, 2, 9].is_sorted());
    assert!(![1, 3, 2, 4].is_sorted());
    assert!([0].is_sorted());
    assert!(empty.is_sorted());
    assert!(![0.0, 1.0, f32::NAN].is_sorted());
}
