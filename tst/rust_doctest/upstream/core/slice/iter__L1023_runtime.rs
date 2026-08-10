// Extracted from library/core/src/slice/iter.rs:1023
#![allow(unused)]
fn main() {
    let mut slice = [11, 22, 33, 0, 44, 55];
    let iter = slice.rsplit_mut(|num| *num == 0);
}
