//@ default-compile-flags: -O
#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

use std::cell::Cell;

struct Parcel<'a>(&'a Cell<u32>, u32);
impl Drop for Parcel<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + self.1);
    }
}

trait Finish {
    fn finish(self, delta: u32) -> u32;
}
impl Finish for Parcel<'_> {
    fn finish(self, delta: u32) -> u32 {
        self.1 + delta
    }
}

#[inline(always)]
fn delta() -> u32 {
    23
}

fn main() {
    let dropped = Cell::new(0);
    let parcel: Box<dyn Finish + '_> = Box::new(Parcel(&dropped, 71));
    assert_eq!((*parcel).finish(delta()), 94);
    assert_eq!(dropped.get(), 71);
}
