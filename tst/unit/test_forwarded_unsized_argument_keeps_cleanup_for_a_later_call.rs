//@ compile-flags: -O -Cpanic=unwind
#![feature(unsized_fn_params)]

use std::cell::Cell;

trait Read { fn read(&self) -> u32; }
struct Payload<'a>(&'a Cell<u32>);
impl Read for Payload<'_> { fn read(&self) -> u32 { 17 } }
impl Drop for Payload<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + 3); }
}

#[inline(never)]
fn finish(value: dyn Read + '_, delta: u32) -> u32 { value.read() + delta }

#[inline(always)]
fn relay(value: dyn Read + '_, delta: u32) -> u32 { finish(value, delta) }

#[inline(never)]
fn delta(order: &Cell<u32>) -> u32 {
    order.set(order.get() * 10 + 6);
    if std::hint::black_box(false) { panic!("later argument failed"); }
    11
}

fn main() {
    let order = Cell::new(0);
    let payload: Box<dyn Read + '_> = Box::new(Payload(&order));
    assert_eq!(relay(*payload, delta(&order)), 28);
    assert_eq!(order.get(), 63);
}
