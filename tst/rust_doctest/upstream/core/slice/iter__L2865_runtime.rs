// Extracted from library/core/src/slice/iter.rs:2865
#![allow(unused)]
fn main() {
    let mut slice = ['l', 'o', 'r', 'e', 'm'];
    let iter = slice.rchunks_exact_mut(2);
}
