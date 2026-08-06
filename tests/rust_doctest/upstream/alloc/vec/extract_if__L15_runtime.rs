// Extracted from library/alloc/src/vec/extract_if.rs:15
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec![0, 1, 2];
    let iter: std::vec::ExtractIf<'_, _, _> = v.extract_if(.., |x| *x % 2 == 0);
}
