// Extracted from library/alloc/src/rc.rs:3322
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let five = Rc::new(5);

    let weak_five = Rc::downgrade(&five);

    let strong_five: Option<Rc<_>> = weak_five.upgrade();
    assert!(strong_five.is_some());

    // Destroy all strong pointers.
    drop(strong_five);
    drop(five);

    assert!(weak_five.upgrade().is_none());
}
