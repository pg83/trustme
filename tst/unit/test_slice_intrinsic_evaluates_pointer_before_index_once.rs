//@ compile-variant: -O
#![feature(core_intrinsics)]
use std::cell::Cell;
fn view<'a>(values: &'a [u16], order: &Cell<u8>) -> &'a [u16] {
    assert_eq!(order.get(), 0);
    order.set(1);
    values
}
fn index(order: &Cell<u8>) -> usize {
    assert_eq!(order.get(), 1);
    order.set(2);
    1
}
fn main() {
    let order = Cell::new(0);
    let values = [103u16, 107];
    let selected: &u16 = unsafe { core::intrinsics::slice_get_unchecked(view(&values, &order), index(&order)) };
    assert_eq!(*selected, 107);
    assert_eq!(order.get(), 2);
}
