// Extracted from library/alloc/src/rc.rs:3223
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    use std::rc::{Rc, Weak};
    use std::alloc::System;

    let strong = Rc::new_in("hello".to_owned(), System);
    let weak = Rc::downgrade(&strong);
    let (raw, alloc) = weak.into_raw_with_allocator();

    assert_eq!(1, Rc::weak_count(&strong));
    assert_eq!("hello", unsafe { &*raw });

    drop(unsafe { Weak::from_raw_in(raw, alloc) });
    assert_eq!(0, Rc::weak_count(&strong));
}
