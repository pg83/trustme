// Extracted from library/core/src/cell.rs:1104
#![allow(unused)]
fn main() {
    use std::cell::RefCell;

    let c = RefCell::new(5);

    {
        let m = c.borrow();
        assert!(c.try_borrow_mut().is_err());
    }

    assert!(c.try_borrow_mut().is_ok());
}
