// Extracted from library/core/src/iter/traits/iterator.rs:3178
#![allow(unused)]
fn main() {
    assert_eq!(
        [2.4, f32::NAN, 1.3]
            .into_iter()
            .reduce(f32::min)
            .unwrap_or(0.),
        1.3
    );
}
