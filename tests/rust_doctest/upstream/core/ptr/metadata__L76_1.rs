// Extracted from library/core/src/ptr/metadata.rs:76
#![allow(unused)]
#![feature(ptr_metadata)]
fn main() {
    
    fn this_never_panics<T: std::ptr::Thin>() {
        assert_eq!(size_of::<&T>(), size_of::<usize>())
    }
}
