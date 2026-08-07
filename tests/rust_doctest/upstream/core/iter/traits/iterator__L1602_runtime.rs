// Extracted from library/core/src/iter/traits/iterator.rs:1602
#![allow(unused)]
#![feature(iter_map_windows)]
fn main() {

    let iter = std::iter::repeat(0).map_windows(|&[]| ());
}
