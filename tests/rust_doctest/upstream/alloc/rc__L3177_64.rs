// Extracted from library/alloc/src/rc.rs:3177
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    use std::ptr;

    let strong = Rc::new("hello".to_owned());
    let weak = Rc::downgrade(&strong);
    // Both point to the same object
    assert!(ptr::eq(&*strong, weak.as_ptr()));
    // The strong here keeps it alive, so we can still access the object.
    assert_eq!("hello", unsafe { &*weak.as_ptr() });

    drop(strong);
    // But not any more. We can do weak.as_ptr(), but accessing the pointer would lead to
    // undefined behavior.
    // assert_eq!("hello", unsafe { &*weak.as_ptr() });
}
