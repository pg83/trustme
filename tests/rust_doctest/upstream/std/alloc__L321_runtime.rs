// Extracted from library/std/src/alloc.rs:321
#![allow(unused)]
#![feature(alloc_error_hook)]
fn main() {
    
    use std::alloc::{Layout, set_alloc_error_hook};
    
    fn custom_alloc_error_hook(layout: Layout) {
       panic!("memory allocation of {} bytes failed", layout.size());
    }
    
    set_alloc_error_hook(custom_alloc_error_hook);
}
