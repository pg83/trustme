// Extracted from library/core/src/iter/traits/iterator.rs:1928
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    let doubled: Vec<i32> = a.iter()
                             .map(|x| x * 2)
                             .collect();

    assert_eq!(vec![2, 4, 6], doubled);
}
