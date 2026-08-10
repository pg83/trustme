// Extracted from library/core/src/iter/traits/iterator.rs:1247
#![allow(unused)]
fn main() {
    let a = [0, 1, 2, -3, 4, 5, -6];

    let iter = a.into_iter().map_while(|x| u32::try_from(x).ok());
    let vec: Vec<_> = iter.collect();

    // We have more elements that could fit in u32 (such as 4, 5), but `map_while` returned `None` for `-3`
    // (as the `predicate` returned `None`) and `collect` stops at the first `None` encountered.
    assert_eq!(vec, [0, 1, 2]);
}
