// Extracted from library/core/src/iter/traits/iterator.rs:1492
#![allow(unused)]
fn main() {
    let data = vec![vec![1, 2, 3, 4], vec![5, 6]];
    let flattened: Vec<_> = data.into_iter().flatten().collect();
    assert_eq!(flattened, [1, 2, 3, 4, 5, 6]);
}
