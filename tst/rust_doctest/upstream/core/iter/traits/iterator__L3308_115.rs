// Extracted from library/core/src/iter/traits/iterator.rs:3308
#![allow(unused)]
fn main() {
    let a = [-3_i32, 0, 1, 5, -10];
    assert_eq!(a.into_iter().min_by(|x, y| x.cmp(y)).unwrap(), -10);
}
