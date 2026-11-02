//@ compile-fail: A constant may not refer to interior mutable data
use std::cell::Cell;
use std::mem::ManuallyDrop;
union Storage {
    plain: u16,
    cell: ManuallyDrop<Cell<u16>>,
}
const VALUE: &Storage = &Storage { plain: 17 };
fn main() { assert_eq!(unsafe { VALUE.plain }, 17); }
