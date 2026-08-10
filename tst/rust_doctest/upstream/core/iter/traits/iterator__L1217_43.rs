// Extracted from library/core/src/iter/traits/iterator.rs:1217
#![allow(unused)]
fn main() {
    let a = [-1i32, 4, 0, 1];

    let mut iter = a.into_iter().map_while(|x| 16i32.checked_div(x));

    assert_eq!(iter.next(), Some(-16));
    assert_eq!(iter.next(), Some(4));
    assert_eq!(iter.next(), None);
}
