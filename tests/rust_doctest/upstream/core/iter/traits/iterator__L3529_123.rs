// Extracted from library/core/src/iter/traits/iterator.rs:3529
#![allow(unused)]
#![feature(iter_array_chunks)]
fn main() {

    let data = [1, 1, 2, -2, 6, 0, 3, 1];
    //          ^-----^  ^------^
    for [x, y, z] in data.iter().array_chunks() {
        assert_eq!(x + y + z, 4);
    }
}
