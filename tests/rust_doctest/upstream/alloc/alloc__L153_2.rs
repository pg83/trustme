// Extracted from library/alloc/src/alloc.rs:153
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::alloc::{alloc_zeroed, dealloc, handle_alloc_error, Layout};

    unsafe {
        let layout = Layout::new::<u16>();
        let ptr = alloc_zeroed(layout);
        if ptr.is_null() {
            handle_alloc_error(layout);
        }

        assert_eq!(*(ptr as *mut u16), 0);

        dealloc(ptr, layout);
    }
}
