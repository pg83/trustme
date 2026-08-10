// Extracted from library/alloc/src/rc.rs:2787
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let original: Box<i32> = Box::new(1);
    let shared: Rc<i32> = Rc::from(original);
    assert_eq!(1, *shared);
}
