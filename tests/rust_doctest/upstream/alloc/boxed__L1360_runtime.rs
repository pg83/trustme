// Extracted from library/alloc/src/boxed.rs:1360
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::alloc::{Allocator, Layout, System};
    use std::ptr::{self, NonNull};

    let x = Box::new_in(String::from("Hello"), System);
    let (ptr, alloc) = Box::into_raw_with_allocator(x);
    unsafe {
        ptr::drop_in_place(ptr);
        let non_null = NonNull::new_unchecked(ptr);
        alloc.deallocate(non_null.cast(), Layout::new::<String>());
    }
}
