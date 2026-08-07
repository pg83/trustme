// Extracted from library/alloc/src/rc.rs:3139
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::{Rc, Weak};

    let strong = Rc::new("hello".to_owned());
    let weak = Rc::downgrade(&strong);
    let raw = weak.into_raw();

    assert_eq!(1, Rc::weak_count(&strong));
    assert_eq!("hello", unsafe { &*raw });

    drop(unsafe { Weak::from_raw(raw) });
    assert_eq!(0, Rc::weak_count(&strong));
}
