//@ crate-type: lib
//@ compile-flags: -C codegen-type=monomir -C panic=abort
//@ output-contains: .mir DROP ZR

use std::cell::Cell;

pub enum Pending<'a> {
    Count(&'a Cell<u32>),
    Empty,
}

impl Drop for Pending<'_> {
    fn drop(&mut self) {
        if let Self::Count(count) = self {
            count.set(count.get() + 1);
        }
    }
}

pub fn release(value: Pending<'_>) {
    drop(value);
}
