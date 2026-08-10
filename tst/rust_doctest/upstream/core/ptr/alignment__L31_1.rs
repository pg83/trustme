// Extracted from library/core/src/ptr/alignment.rs:31
#![allow(unused)]
#![feature(ptr_alignment_type)]
fn main() {
    use std::ptr::Alignment;

    assert_eq!(Alignment::MIN.as_usize(), 1);
}
