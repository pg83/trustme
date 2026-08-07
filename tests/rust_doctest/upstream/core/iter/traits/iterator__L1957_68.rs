// Extracted from library/core/src/iter/traits/iterator.rs:1957
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    let doubled = a.iter().map(|x| x * 2).collect::<Vec<i32>>();

    assert_eq!(vec![2, 4, 6], doubled);
}
