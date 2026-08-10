// Extracted from library/core/src/cell.rs:1063
#![allow(unused)]
fn main() {
    use std::cell::RefCell;

    let c = RefCell::new("hello".to_owned());

    *c.borrow_mut() = "bonjour".to_owned();

    assert_eq!(&*c.borrow(), "bonjour");
}
