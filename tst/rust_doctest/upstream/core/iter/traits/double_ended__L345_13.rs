// Extracted from library/core/src/iter/traits/double_ended.rs:345
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    let mut iter = a.iter();

    assert_eq!(iter.rfind(|&&x| x == 2), Some(&2));

    // we can still use `iter`, as there are more elements.
    assert_eq!(iter.next_back(), Some(&1));
}
