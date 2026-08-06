// Extracted from library/core/src/iter/traits/iterator.rs:3940
#![allow(unused)]
fn main() {
    assert!([1, 2, 2, 9].iter().is_sorted());
    assert!(![1, 3, 2, 4].iter().is_sorted());
    assert!([0].iter().is_sorted());
    assert!(std::iter::empty::<i32>().is_sorted());
    assert!(![0.0, 1.0, f32::NAN].iter().is_sorted());
}
