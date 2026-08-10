// Extracted from library/alloc/src/rc.rs:2599
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    use std::cmp::Ordering;

    let five = Rc::new(5);

    assert_eq!(Ordering::Less, five.cmp(&Rc::new(6)));
}
