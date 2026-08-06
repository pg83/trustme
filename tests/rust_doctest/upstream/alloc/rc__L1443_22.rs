// Extracted from library/alloc/src/rc.rs:1443
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    use std::alloc::System;
    
    let x = Rc::new_in("hello".to_owned(), System);
    let (ptr, alloc) = Rc::into_raw_with_allocator(x);
    assert_eq!(unsafe { &*ptr }, "hello");
    let x = unsafe { Rc::from_raw_in(ptr, alloc) };
    assert_eq!(&*x, "hello");
}
