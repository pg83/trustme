// Extracted from library/core/src/slice/mod.rs:3454
#![allow(unused)]
#![feature(slice_partition_dedup)]
fn main() {
    
    let mut slice = ["foo", "Foo", "BAZ", "Bar", "bar", "baz", "BAZ"];
    
    let (dedup, duplicates) = slice.partition_dedup_by(|a, b| a.eq_ignore_ascii_case(b));
    
    assert_eq!(dedup, ["foo", "BAZ", "Bar", "baz"]);
    assert_eq!(duplicates, ["bar", "Foo", "BAZ"]);
}
