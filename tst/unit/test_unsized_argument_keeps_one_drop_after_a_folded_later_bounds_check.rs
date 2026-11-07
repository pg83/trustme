//@ compile-flags: -O -Cpanic=unwind
#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

use std::cell::Cell;

struct Ticket<'a>(&'a Cell<u32>);

impl Drop for Ticket<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

trait Read {
    fn read(&self) -> u16;
}

impl Read for Ticket<'_> {
    fn read(&self) -> u16 { 797 }
}

#[inline(never)]
fn finish<'a>(ticket: dyn Read + 'a, extra: u16) -> u16 {
    ticket.read() + extra
}

fn main() {
    let drops = Cell::new(0);
    let ticket: Box<dyn Read + '_> = Box::new(Ticket(&drops));
    let table = [11u16, 13u16];
    let index = 1usize;
    assert_eq!(finish(*ticket, table[index]), 810);
    assert_eq!(drops.get(), 1);
}
