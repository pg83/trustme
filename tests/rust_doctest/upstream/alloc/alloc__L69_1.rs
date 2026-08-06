// Extracted from library/alloc/src/alloc.rs:69
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::alloc::{alloc, dealloc, handle_alloc_error, Layout};
    
    unsafe {
        let layout = Layout::new::<u16>();
        let ptr = alloc(layout);
        if ptr.is_null() {
            handle_alloc_error(layout);
        }
    
        *(ptr as *mut u16) = 42;
        assert_eq!(*(ptr as *mut u16), 42);
    
        dealloc(ptr, layout);
    }
}
