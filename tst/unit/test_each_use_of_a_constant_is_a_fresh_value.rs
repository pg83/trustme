// A constant is copied at every use: setting a `Cell` in one use of a
// constant array is not seen by the next, however the value is stored.
use std::cell::Cell;

#[allow(clippy::declare_interior_mutable_const)]
const CELLS: [Cell<u64>; 8] = [const { Cell::new(1) }; 8];

fn main() {
    #[allow(clippy::borrow_interior_mutable_const)]
    CELLS[3].set(5);
    assert_eq!(CELLS[3].get(), 1);
    let copy = CELLS;
    copy[3].set(9);
    assert_eq!(copy[3].get(), 9);
    assert_eq!(CELLS[3].get(), 1);
}
