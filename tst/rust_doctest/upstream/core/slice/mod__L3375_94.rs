// Extracted from library/core/src/slice/mod.rs:3375
#![allow(unused)]
fn main() {
    let mut v = [-5i32, 4, 1, -3, 2];

    // Find the items `<=` to the absolute median, the absolute median itself, and the items
    // `>=` to it.
    let (lesser, median, greater) = v.select_nth_unstable_by_key(2, |a| a.abs());

    assert!(lesser == [1, 2] || lesser == [2, 1]);
    assert_eq!(median, &mut -3);
    assert!(greater == [4, -5] || greater == [-5, 4]);

    // We are only guaranteed the slice will be one of the following, based on the way we sort
    // about the specified index.
    assert!(v == [1, 2, -3, 4, -5] ||
            v == [1, 2, -3, -5, 4] ||
            v == [2, 1, -3, 4, -5] ||
            v == [2, 1, -3, -5, 4]);
}
