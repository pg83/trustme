// Extracted from library/core/src/slice/mod.rs:1082
#![allow(unused)]
fn main() {
    let slice = ['f', 'o', 'o'];
    let mut iter = slice.windows(4);
    assert!(iter.next().is_none());
}
