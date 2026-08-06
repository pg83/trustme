// Extracted from library/core/src/slice/mod.rs:2871
#![allow(unused)]
fn main() {
    let mut s = vec![0, 1, 1, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55];
    let num = 42;
    let idx = s.partition_point(|&x| x <= num);
    // If `num` is unique, `s.partition_point(|&x| x < num)` (with `<`) is equivalent to
    // `s.binary_search(&num).unwrap_or_else(|x| x)`, but using `<=` will allow `insert`
    // to shift less elements.
    s.insert(idx, num);
    assert_eq!(s, [0, 1, 1, 1, 1, 2, 3, 5, 8, 13, 21, 34, 42, 55]);
}
