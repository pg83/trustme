//@ reject-flags: -Zdump-hir --cfg=dump_before_type_error || --cfg=dump_before_type_error => Type mismatch || mismatched types
#![feature(ergonomic_clones)]
use std::{cell::Cell, clone::UseCloned, rc::Rc};

struct Ticket {
    copies: Rc<Cell<usize>>,
    serial: u32,
}
impl Clone for Ticket {
    fn clone(&self) -> Self {
        self.copies.set(self.copies.get() + 1);
        Self { copies: self.copies.clone(), serial: self.serial }
    }
}
impl UseCloned for Ticket {}

#[cfg(dump_before_type_error)]
fn rejected_after_the_hir_dump() {
    let _: u8 = "not a byte";
}

fn main() {
    let copies = Rc::new(Cell::new(0));
    let first = Ticket { copies: copies.clone(), serial: 47 };
    let second = first.use;
    let third = second.use;
    assert_eq!(copies.get(), 2);
    assert_eq!(first.serial + second.serial + third.serial, 141);
    drop(first);
    assert_eq!(second.serial, third.serial);
    assert_eq!(copies.get(), 2);
}
