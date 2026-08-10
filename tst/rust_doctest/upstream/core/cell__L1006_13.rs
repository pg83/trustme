// Extracted from library/core/src/cell.rs:1006
#![allow(unused)]
fn main() {
    use std::cell::RefCell;

    let c = RefCell::new(5);

    {
        let m = c.borrow_mut();
        assert!(c.try_borrow().is_err());
    }

    {
        let m = c.borrow();
        assert!(c.try_borrow().is_ok());
    }
}
