// Extracted from library/core/src/slice/iter.rs:670
#![allow(unused)]
fn main() {
    let mut v = [10, 40, 30, 20, 60, 50];
    let iter = v.split_mut(|num| *num % 3 == 0);
}
