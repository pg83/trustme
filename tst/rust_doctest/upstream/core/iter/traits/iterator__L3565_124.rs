// Extracted from library/core/src/iter/traits/iterator.rs:3565
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    let sum: i32 = a.iter().sum();

    assert_eq!(sum, 6);

    let b: Vec<f32> = vec![];
    let sum: f32 = b.iter().sum();
    assert_eq!(sum, -0.0_f32);
}
