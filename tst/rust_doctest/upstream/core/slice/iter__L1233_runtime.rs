// Extracted from library/core/src/slice/iter.rs:1233
#![allow(unused)]
fn main() {
    let mut slice = [10, 40, 30, 20, 60, 50];
    let iter = slice.splitn_mut(2, |num| *num % 3 == 0);
}
