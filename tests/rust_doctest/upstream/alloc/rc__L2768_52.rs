// Extracted from library/alloc/src/rc.rs:2768
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let original: String = "statue".to_owned();
    let shared: Rc<str> = Rc::from(original);
    assert_eq!("statue", &shared[..]);
}
