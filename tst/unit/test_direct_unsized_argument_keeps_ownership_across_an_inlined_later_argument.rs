//@ compile-flags: -O
#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

use std::cell::Cell;

struct Meter<'a> {
    drops: &'a Cell<u16>,
    word: u16,
}
impl Drop for Meter<'_> {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}
trait Peek {
    fn peek(&self) -> u16;
}
impl Peek for Meter<'_> {
    fn peek(&self) -> u16 { self.word }
}
#[inline(never)]
fn finish<'a>(meter: dyn Peek + 'a, extra: u16) -> u16 {
    meter.peek() + extra
}
#[inline(always)]
fn extra() -> u16 { 19 }

fn main() {
    let drops = Cell::new(0);
    let meter: Box<dyn Peek + '_> = Box::new(Meter { drops: &drops, word: 331 });
    assert_eq!(finish(*meter, extra()), 350);
    assert_eq!(drops.get(), 1);
}
