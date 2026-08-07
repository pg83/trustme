// Extracted from library/core/src/slice/mod.rs:3244
#![allow(unused)]
fn main() {
    let mut v = [-5i32, 4, 2, -3, 1];

    // Find the items `<=` to the median, the median itself, and the items `>=` to it.
    let (lesser, median, greater) = v.select_nth_unstable(2);

    assert!(lesser == [-3, -5] || lesser == [-5, -3]);
    assert_eq!(median, &mut 1);
    assert!(greater == [4, 2] || greater == [2, 4]);

    // We are only guaranteed the slice will be one of the following, based on the way we sort
    // about the specified index.
    assert!(v == [-3, -5, 1, 2, 4] ||
            v == [-5, -3, 1, 2, 4] ||
            v == [-3, -5, 1, 4, 2] ||
            v == [-5, -3, 1, 4, 2]);
}
