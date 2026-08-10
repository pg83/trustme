// Extracted from library/alloc/src/rc.rs:2354
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let x: Rc<i32> = Default::default();
    assert_eq!(*x, 0);
}
