// Extracted from library/core/src/iter/sources/once.rs:17
#![allow(unused)]
fn main() {
    use std::iter;

    // one is the loneliest number
    let mut one = iter::once(1);

    assert_eq!(Some(1), one.next());

    // just one, that's all we get
    assert_eq!(None, one.next());
}
