// Extracted from library/core/src/iter/traits/iterator.rs:1104
#![allow(unused)]
fn main() {
    let a = [-1, 0, 1, -2];

    let mut iter = a.into_iter().skip_while(|&x| x < 0);

    assert_eq!(iter.next(), Some(0));
    assert_eq!(iter.next(), Some(1));

    // while this would have been false, since we already got a false,
    // skip_while() isn't used any more
    assert_eq!(iter.next(), Some(-2));

    assert_eq!(iter.next(), None);
}
