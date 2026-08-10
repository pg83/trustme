// Extracted from library/core/src/slice/mod.rs:3619
#![allow(unused)]
fn main() {
    let mut a = ['a', 'b', 'c', 'd', 'e', 'f'];
    a.rotate_left(2);
    assert_eq!(a, ['c', 'd', 'e', 'f', 'a', 'b']);
}
