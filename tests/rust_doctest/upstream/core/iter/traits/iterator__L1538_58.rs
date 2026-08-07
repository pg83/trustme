// Extracted from library/core/src/iter/traits/iterator.rs:1538
#![allow(unused)]
fn main() {
    let d3 = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]];

    let d2: Vec<_> = d3.into_iter().flatten().collect();
    assert_eq!(d2, [[1, 2], [3, 4], [5, 6], [7, 8]]);

    let d1: Vec<_> = d3.into_iter().flatten().flatten().collect();
    assert_eq!(d1, [1, 2, 3, 4, 5, 6, 7, 8]);
}
