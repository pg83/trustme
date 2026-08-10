// Extracted from library/core/src/slice/iter.rs:421
#![allow(unused)]
#![feature(split_as_slice)]
fn main() {
    let slice = [1,2,3,4,5];
    let mut split = slice.split(|v| v % 2 == 0);
    assert!(split.next().is_some());
    assert_eq!(split.as_slice(), &[3,4,5]);
}
