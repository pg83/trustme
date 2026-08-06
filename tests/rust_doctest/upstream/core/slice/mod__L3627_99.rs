// Extracted from library/core/src/slice/mod.rs:3627
#![allow(unused)]
fn main() {
    let mut a = ['a', 'b', 'c', 'd', 'e', 'f'];
    a[1..5].rotate_left(1);
    assert_eq!(a, ['a', 'c', 'd', 'e', 'b', 'f']);
}
