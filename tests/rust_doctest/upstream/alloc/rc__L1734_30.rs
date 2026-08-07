// Extracted from library/alloc/src/rc.rs:1734
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let mut x = Rc::new(3);
    *Rc::get_mut(&mut x).unwrap() = 4;
    assert_eq!(*x, 4);

    let _y = Rc::clone(&x);
    assert!(Rc::get_mut(&mut x).is_none());
}
