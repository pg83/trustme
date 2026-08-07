// Extracted from library/core/src/iter/sources/repeat_n.rs:20
#![allow(unused)]
fn main() {
    use std::iter;

    // four of the number four:
    let mut four_fours = iter::repeat_n(4, 4);

    assert_eq!(Some(4), four_fours.next());
    assert_eq!(Some(4), four_fours.next());
    assert_eq!(Some(4), four_fours.next());
    assert_eq!(Some(4), four_fours.next());

    // no more fours
    assert_eq!(None, four_fours.next());
}
