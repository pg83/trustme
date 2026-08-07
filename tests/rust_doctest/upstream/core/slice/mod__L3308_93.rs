// Extracted from library/core/src/slice/mod.rs:3308
#![allow(unused)]
fn main() {
    let mut v = [-5i32, 4, 2, -3, 1];

    // Find the items `>=` to the median, the median itself, and the items `<=` to it, by using
    // a reversed comparator.
    let (before, median, after) = v.select_nth_unstable_by(2, |a, b| b.cmp(a));

    assert!(before == [4, 2] || before == [2, 4]);
    assert_eq!(median, &mut 1);
    assert!(after == [-3, -5] || after == [-5, -3]);

    // We are only guaranteed the slice will be one of the following, based on the way we sort
    // about the specified index.
    assert!(v == [2, 4, 1, -5, -3] ||
            v == [2, 4, 1, -3, -5] ||
            v == [4, 2, 1, -5, -3] ||
            v == [4, 2, 1, -3, -5]);
}
