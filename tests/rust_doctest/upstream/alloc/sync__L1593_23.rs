// Extracted from library/alloc/src/sync.rs:1593
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    use std::alloc::System;
    
    let x = Arc::new_in("hello".to_owned(), System);
    let (ptr, alloc) = Arc::into_raw_with_allocator(x);
    assert_eq!(unsafe { &*ptr }, "hello");
    let x = unsafe { Arc::from_raw_in(ptr, alloc) };
    assert_eq!(&*x, "hello");
}
