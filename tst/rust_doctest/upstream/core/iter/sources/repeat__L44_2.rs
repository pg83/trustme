// Extracted from library/core/src/iter/sources/repeat.rs:44
#![allow(unused)]
fn main() {
    use std::iter;

    // that last example was too many fours. Let's only have four fours.
    let mut four_fours = iter::repeat(4).take(4);

    assert_eq!(Some(4), four_fours.next());
    assert_eq!(Some(4), four_fours.next());
    assert_eq!(Some(4), four_fours.next());
    assert_eq!(Some(4), four_fours.next());

    // ... and now we're done
    assert_eq!(None, four_fours.next());
}
