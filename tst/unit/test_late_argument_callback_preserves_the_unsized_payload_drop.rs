//@ compile-flags: -O
#![feature(core_intrinsics, unsized_fn_params)]

use std::cell::Cell;

trait Payload { fn number(&self) -> u32; }

struct Tracked<'a>(&'a Cell<u32>);

impl Payload for Tracked<'_> {
    fn number(&self) -> u32 { 37 }
}

impl Drop for Tracked<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}

const fn constant_extra() -> u32 { 3 }
fn runtime_extra() -> u32 { 41 }

fn consume(value: dyn Payload, extra: u32) -> u32 {
    value.number() + extra
}

#[inline(never)]
fn run(count: &Cell<u32>) -> u32 {
    let value: Box<dyn Payload + '_> = Box::new(Tracked(count));
    consume(*value, unsafe {
        core::intrinsics::const_eval_select((), constant_extra, runtime_extra)
    })
}

fn main() {
    let count = Cell::new(0);
    assert_eq!(run(&count), 78);
    assert_eq!(count.get(), 1);
}
