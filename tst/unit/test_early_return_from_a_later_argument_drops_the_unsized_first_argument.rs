//@ compile-flags: -O
#![feature(unsized_fn_params)]

use std::cell::Cell;

trait Payload {}
struct Tracked<'a>(&'a Cell<u32>);
impl Payload for Tracked<'_> {}
impl Drop for Tracked<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}

fn consume(_: dyn Payload, _: u32) {}

#[inline(never)]
fn leave(count: &Cell<u32>) -> u32 {
    let value: Box<dyn Payload + '_> = Box::new(Tracked(count));
    consume(*value, return 37);
    0
}

fn main() {
    let count = Cell::new(0);
    assert_eq!(leave(&count), 37);
    assert_eq!(count.get(), 1);
}
