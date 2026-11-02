//@ compile-variant: -O
use std::cell::Cell;
use std::mem::ManuallyDrop;

union Storage {
    cell: ManuallyDrop<Cell<u16>>,
    plain: u16,
}

fn main() {
    let first = &Storage { plain: 17 };
    let second = &Storage { cell: ManuallyDrop::new(Cell::new(23)) };
    assert_eq!(unsafe { first.plain }, 17);
    unsafe { second.cell.set(29) };
    assert_eq!(unsafe { second.cell.get() }, 29);
}
