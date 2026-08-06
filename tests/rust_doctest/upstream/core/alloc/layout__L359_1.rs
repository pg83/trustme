// Extracted from library/core/src/alloc/layout.rs:359
#![allow(unused)]
#![feature(alloc_layout_extra)]
fn main() {
    use std::alloc::Layout;
    
    // All rust types have a size that's a multiple of their alignment.
    let normal = Layout::from_size_align(12, 4).unwrap();
    let repeated = normal.repeat(3).unwrap();
    assert_eq!(repeated, (Layout::from_size_align(36, 4).unwrap(), 12));
    
    // But you can manually make layouts which don't meet that rule.
    let padding_needed = Layout::from_size_align(6, 4).unwrap();
    let repeated = padding_needed.repeat(3).unwrap();
    assert_eq!(repeated, (Layout::from_size_align(24, 4).unwrap(), 8));
}
