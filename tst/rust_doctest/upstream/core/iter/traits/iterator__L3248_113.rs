// Extracted from library/core/src/iter/traits/iterator.rs:3248
#![allow(unused)]
fn main() {
    let a = [-3_i32, 0, 1, 5, -10];
    assert_eq!(a.into_iter().max_by(|x, y| x.cmp(y)).unwrap(), 5);
}
