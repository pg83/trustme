// Extracted from library/alloc/src/slice.rs:175
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = [4, -5, 1, -3, 2];
    v.sort_by(|a, b| a.cmp(b));
    assert_eq!(v, [-5, -3, 1, 2, 4]);

    // reverse sorting
    v.sort_by(|a, b| b.cmp(a));
    assert_eq!(v, [4, 2, 1, -3, -5]);
}
