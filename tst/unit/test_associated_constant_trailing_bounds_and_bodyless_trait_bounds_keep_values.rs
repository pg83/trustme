#![feature(generic_const_items)]
#![allow(incomplete_features)]

trait Widths {
    const WIDTH<T>: usize where T: Copy;
}

struct Table;

impl Widths for Table {
    const WIDTH<T>: usize = core::mem::size_of::<T>() where T: Copy;
}

impl Table {
    const STRIDE<T>: usize = core::mem::size_of::<[T; 3]>() where T: Copy;
}

fn main() {
    assert_eq!(<Table as Widths>::WIDTH::<u32>, 4);
    assert_eq!(Table::STRIDE::<u16>, 6);
    assert_eq!(Table::STRIDE::<[u8; 4]>, 12);
}
