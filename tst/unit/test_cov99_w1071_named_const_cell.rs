use std::cell::Cell;

struct Holder {
    cell: Cell<u8>,
}

const INITIAL: Holder = Holder { cell: Cell::new(7) };

fn main() {
    let holder = &INITIAL;
    assert_eq!(holder.cell.get(), 7);
    holder.cell.set(11);
    assert_eq!(holder.cell.get(), 11);
}
