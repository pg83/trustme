// Extracted from library/alloc/src/rc.rs:1471
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let x = Rc::new(0);
    let y = Rc::clone(&x);
    let x_ptr = Rc::as_ptr(&x);
    assert_eq!(x_ptr, Rc::as_ptr(&y));
    assert_eq!(unsafe { *x_ptr }, 0);
}
