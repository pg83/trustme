// Extracted from library/core/src/convert/mod.rs:95
#![allow(unused)]
fn main() {
    use std::convert::identity;

    let iter = [Some(1), None, Some(3)].into_iter();
    let filtered = iter.filter_map(identity).collect::<Vec<_>>();
    assert_eq!(vec![1, 3], filtered);
}
