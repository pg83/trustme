// Extracted from library/core/src/slice/mod.rs:3420
#![allow(unused)]
#![feature(slice_partition_dedup)]
fn main() {

    let mut slice = [1, 2, 2, 3, 3, 2, 1, 1];

    let (dedup, duplicates) = slice.partition_dedup();

    assert_eq!(dedup, [1, 2, 3, 2, 1]);
    assert_eq!(duplicates, [2, 3, 1]);
}
