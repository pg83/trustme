// Extracted from library/core/src/iter/traits/iterator.rs:3215
#![allow(unused)]
fn main() {
    let a = [-3_i32, 0, 1, 5, -10];
    assert_eq!(a.into_iter().max_by_key(|x| x.abs()).unwrap(), -10);
}
