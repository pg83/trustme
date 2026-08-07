// Extracted from library/alloc/src/rc.rs:1878
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let mut data = Rc::new(75);
    let weak = Rc::downgrade(&data);

    assert!(75 == *data);
    assert!(75 == *weak.upgrade().unwrap());

    *Rc::make_mut(&mut data) += 1;

    assert!(76 == *data);
    assert!(weak.upgrade().is_none());
}
