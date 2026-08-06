// Extracted from library/core/src/slice/mod.rs:1047
#![allow(unused)]
fn main() {
    let x = &mut [1, 2, 4];
    for elem in x.iter_mut() {
        *elem += 2;
    }
    assert_eq!(x, &[3, 4, 6]);
}
