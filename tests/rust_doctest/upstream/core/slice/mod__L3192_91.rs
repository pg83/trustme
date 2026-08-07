// Extracted from library/core/src/slice/mod.rs:3192
#![allow(unused)]
fn main() {
    let mut v = [4i32, -5, 1, -3, 2];

    v.sort_unstable_by_key(|k| k.abs());
    assert_eq!(v, [1, 2, -3, 4, -5]);
}
