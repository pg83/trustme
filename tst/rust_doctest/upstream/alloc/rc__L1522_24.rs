// Extracted from library/alloc/src/rc.rs:1522
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::rc::Rc;
    use std::alloc::System;

    let x = Rc::new_in("hello".to_owned(), System);
    let (x_ptr, _alloc) = Rc::into_raw_with_allocator(x);

    unsafe {
        // Convert back to an `Rc` to prevent leak.
        let x = Rc::from_raw_in(x_ptr, System);
        assert_eq!(&*x, "hello");

        // Further calls to `Rc::from_raw(x_ptr)` would be memory-unsafe.
    }

    // The memory was freed when `x` went out of scope above, so `x_ptr` is now dangling!
}
