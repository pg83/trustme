// Extracted from library/core/src/ptr/alignment.rs:116
#![allow(unused)]
#![feature(ptr_alignment_type)]
fn main() {
    use std::ptr::Alignment;

    assert_eq!(Alignment::of::<u8>().log2(), 0);
    assert_eq!(Alignment::new(1024).unwrap().log2(), 10);
}
