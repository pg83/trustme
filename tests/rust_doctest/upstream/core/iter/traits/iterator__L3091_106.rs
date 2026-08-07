// Extracted from library/core/src/iter/traits/iterator.rs:3091
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    assert_eq!(a.into_iter().rposition(|x| x == 3), Some(2));

    assert_eq!(a.into_iter().rposition(|x| x == 5), None);
}
