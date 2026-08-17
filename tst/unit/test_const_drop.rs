//@ edition: 2024
// A destructor may run during constant evaluation when the impl is `const`, and
// its effects are visible to the rest of the constant: `RefCell`'s guards are
// what make `borrow` usable in a `const` block.
#![feature(const_ref_cell, const_destruct, const_convert, const_trait_impl)]

use std::cell::RefCell;

const BORROWS: (bool, bool, i32) = {
    let cell = RefCell::new(7);
    // The guard from a `try_borrow_mut` is dropped at the end of the statement,
    // so the next borrow succeeds.
    let mutable_first = cell.try_borrow_mut().is_ok();
    let shared = cell.borrow();
    let blocked_while_shared = cell.try_borrow_mut().is_err();
    let value = *shared;
    (mutable_first, blocked_while_shared, value)
};

const REPLACED: i32 = {
    let cell = RefCell::new(0);
    let previous = cell.replace(10);
    assert!(previous == 0);
    cell.into_inner()
};

fn main() {
    assert_eq!(BORROWS, (true, true, 7));
    assert_eq!(REPLACED, 10);

    // The same code at run time agrees.
    let cell = RefCell::new(7);
    assert!(cell.try_borrow_mut().is_ok());
    let shared = cell.borrow();
    assert!(cell.try_borrow_mut().is_err());
    assert_eq!(*shared, 7);
}
