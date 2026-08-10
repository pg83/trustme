// Extracted from library/core/src/slice/mod.rs:967
#![allow(unused)]
fn main() {
    let mut v = [1, 2, 3];
    v.reverse();
    assert!(v == [3, 2, 1]);
}
