//@ compile-fail: where clauses are not allowed before const item bodies
#![feature(generic_const_items)]
#![allow(incomplete_features)]

struct Table;

impl Table {
    const WIDTH<T>: usize where T: Copy = core::mem::size_of::<T>();
}

fn main() {
    let _ = Table::WIDTH::<u16>;
}
