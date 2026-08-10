// Extracted from library/core/src/iter/traits/iterator.rs:575
#![allow(unused)]
fn main() {
    use std::iter::zip;

    let a = [1, 2, 3];
    let b = [2, 3, 4];

    let mut zipped = zip(
        a.into_iter().map(|x| x * 2).skip(1),
        b.into_iter().map(|x| x * 2).skip(1),
    );

    assert_eq!(zipped.next(), Some((4, 6)));
    assert_eq!(zipped.next(), Some((6, 8)));
    assert_eq!(zipped.next(), None);
}
