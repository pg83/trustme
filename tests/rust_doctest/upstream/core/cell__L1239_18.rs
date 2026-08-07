// Extracted from library/core/src/cell.rs:1239
#![allow(unused)]
fn main() {
    use std::cell::RefCell;

    let c = RefCell::new(5);

    {
        let m = c.borrow_mut();
        assert!(unsafe { c.try_borrow_unguarded() }.is_err());
    }

    {
        let m = c.borrow();
        assert!(unsafe { c.try_borrow_unguarded() }.is_ok());
    }
}
