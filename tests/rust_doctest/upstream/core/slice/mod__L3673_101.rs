// Extracted from library/core/src/slice/mod.rs:3673
#![allow(unused)]
fn main() {
    let mut a = ['a', 'b', 'c', 'd', 'e', 'f'];
    a[1..5].rotate_right(1);
    assert_eq!(a, ['a', 'e', 'b', 'c', 'd', 'f']);
}
