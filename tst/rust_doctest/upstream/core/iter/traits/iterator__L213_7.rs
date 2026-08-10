// Extracted from library/core/src/iter/traits/iterator.rs:213
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    assert_eq!(a.iter().count(), 3);

    let a = [1, 2, 3, 4, 5];
    assert_eq!(a.iter().count(), 5);
}
