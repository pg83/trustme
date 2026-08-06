// Extracted from library/core/src/slice/mod.rs:3580
#![allow(unused)]
#![feature(slice_partition_dedup)]
fn main() {
    
    let mut slice = [10, 20, 21, 30, 30, 20, 11, 13];
    
    let (dedup, duplicates) = slice.partition_dedup_by_key(|i| *i / 10);
    
    assert_eq!(dedup, [10, 20, 30, 20, 11]);
    assert_eq!(duplicates, [21, 30, 13]);
}
