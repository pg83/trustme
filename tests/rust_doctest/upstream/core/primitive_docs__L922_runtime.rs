// Extracted from library/core/src/primitive_docs.rs:922
#![allow(unused)]
fn main() {
    let mut scores: &mut [i32] = &mut [7, 8, 9];
    for score in scores {
        *score += 1;
    }
}
