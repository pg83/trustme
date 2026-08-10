// Extracted from library/core/src/array/mod.rs:102
#![allow(unused)]
fn main() {
    let mut state = 1;
    let a = std::array::from_fn(|_| { let x = state; state *= 2; x });
    assert_eq!(a, [1, 2, 4, 8, 16, 32]);
}
