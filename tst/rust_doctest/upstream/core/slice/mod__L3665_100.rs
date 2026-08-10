// Extracted from library/core/src/slice/mod.rs:3665
#![allow(unused)]
fn main() {
    let mut a = ['a', 'b', 'c', 'd', 'e', 'f'];
    a.rotate_right(2);
    assert_eq!(a, ['e', 'f', 'a', 'b', 'c', 'd']);
}
